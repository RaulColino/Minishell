#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include "parser.h"

/*
Para compilar:  
'gcc -Wall -Wextra test.c libparser.a -o test -static'

#include <stdlib.h> para poder utilizar exit(), malloc(), atoi(), etc.
#include <unistd.h> para poder tilizar dup(), getpid(), exec(), chdir(), etc.
#include <errno.h> para poder utilizar la variable errno y obtener mensajes de error
#include <sys/types.h> para poder utilizar fork(), variables pid_t, etc.  
#include <fcntl.h> para manejo de ficheros. Lo necesitamos para usar la funcion open().
*/

/*--Declaracion de variables y constantes -------------------------------------------*/

/*Capacidad maxima para el buffer*/
#define T_MAX_LINEA 1024
/*Capacidad maxima para la lista de procesos en background*/
#define MAX_PROCESOS_BG 1024
/*Estructura/registro que contiene el pid y la linea ejecutada de un proceso */
typedef struct Proceso
{
    pid_t id;
    char *nombre;
} Tproceso;
/*Variable que controla el bucle del programa principal*/
int finalizar = 0;
/*Variable para almacenar la linea introducida por el usuario*/
static char buffer[T_MAX_LINEA];
/*Lista de procesos en background*/
static Tproceso listabg[MAX_PROCESOS_BG];
/*Numero de procesos en background*/
static int nProcesosbg = 0;
/*PID del proceso que se ejecuta en primer plano. -1 si solo es la shell.*/
pid_t pidProcesofg = -1;

/*--Declaracion de funciones ---------------------------------------------------------*/

void manejadorProcesos(int sig);
void manejadorProcesosbgTerminados(int sig);

void procesarLinea(tline *linea);
void cambiarDirectorio(tcommand comando);
void ejecutarEnForeground(tline *linea);

int agregarProcesobg(pid_t pid, char *lineaC);
int eliminarProcesobg(int pid);
void mostrarProcesosbg();

void redireccionarEntrada(char *nombreFichero);
void redireccionarSalida(char *nombreFichero);
void redireccionarError(char *nombreFichero);

void ejecutarComando(tline *linea);
void ejecutarComandos(tline *linea);

/*--Programa principal ---------------------------------------------------------------*/

int main(void)
{
    /*Solo los procesos en primer plano, excepto la shell, responden a SIGINT y SIGQUIT*/
    signal(SIGINT, manejadorProcesos);
    signal(SIGQUIT, manejadorProcesos);

    /*Si algun hijo en background termina, el manejador correspondiente actua*/
    signal(SIGCHLD, manejadorProcesosbgTerminados);

    /*Bucle que se ejecuta mientras el usuario no indique la finalizacion del programa*/
    while (finalizar == 0)
    {
        /*Mostramos el prompt*/
        printf("msh> ");

        /*Leemos la entrada y la procesamos*/
        fgets(buffer, T_MAX_LINEA, stdin);
        procesarLinea(tokenize(buffer));
    }
    return 0;
}

/*--Implementacion de funciones ------------------------------------------------------*/

/*Los procesos hijos en foreground deben poder acabarse con SIGINT y SIGQUIT*/
void manejadorProcesos(int sig)
{
    pid_t pidRecibido;

    /*Si hay algun proceso ejecutandose en foreground*/
    if (pidProcesofg != -1)
    {
        printf("El proceso en fg es %d", pidProcesofg);
        if (kill(-pidProcesofg, SIGTERM) < 0)
        {
            fprintf(stderr, "Error al intentar terminar el proceso de PID  %d: %s\n", pidProcesofg, strerror(errno));
            printf("Se va a forzar el cierre\n");
            sleep(1);
            pidRecibido = waitpid(pidProcesofg, NULL, WNOHANG);
            if (pidRecibido == -1)
            {
                if (kill(-pidProcesofg, SIGKILL) < 0)
                {
                    fprintf(stderr, "Error de SIGKILL al proceso de PID  %d: %s\n", pidProcesofg, strerror(errno));
                }
            }
        }
    }
    return;
}

/*Manejador para liberar los recursos asociados a procesos que terminaron su ejecucion*/
void manejadorProcesosbgTerminados(int sig)
{
    pid_t pidHijo;
    int status;

    if (sig == SIGCHLD)
    {

        pidHijo = waitpid(-1, &status, WNOHANG);
        /*Si un proceso ha terminado*/
        if (pidHijo != -1 && pidHijo != 0)
        {
            /*y resulta que era un proceso en segundo plano, se elimina de 'listabg'*/
            if (eliminarProcesobg(pidHijo) != 1)
            {
                printf("Acabo el proceso en background de pid %d.\n", pidHijo);
                /*si el hijo termino de una manera normal*/
                if (WIFEXITED(status) != 0)
                {
                    /*se notifica con un error si el hijo no se ejecuto correctamente*/
                    if (WEXITSTATUS(status) != 0)
                    {
                        printf("Termino de forma normal pero con un error: El comando NO se ejecutó correctamente.\n");
                    }
                }
                else
                {
                    printf("Termino de forma anormal.\n");
                }
            }
        }
    }
    return;
}

/*Funcion que realiza la accion correspondiente en funcion de la entrada del usuario*/
void procesarLinea(tline *linea)
{
    char *nombreMandato;
    int comandoExterno = 0;

    /*Si se ha introducido un comando, ya sea interno o externo...*/
    if (linea->ncommands == 1)
    {
        nombreMandato = linea->commands[0].argv[0];

        /* Si el mandato es cd...*/
        if (strcmp(nombreMandato, "cd") == 0)
        {
            cambiarDirectorio(linea->commands[0]);
        }
        /*Si el mandato es exit...*/
        else if (strcmp(nombreMandato, "exit") == 0)
        {
            /* Cambiamos a 1 'finalizar' para que termine el bucle del programa principal */
            finalizar = 1;
        }
        /*Si el mandato es jobs*/
        else if (strcmp(nombreMandato, "jobs") == 0)
        {
            mostrarProcesosbg();
        }
        /*Si el mandato es fg*/
        else if (strcmp(nombreMandato, "fg") == 0)
        {
            ejecutarEnForeground(linea);
        }
        else
        {

            comandoExterno = 1;
        }
    }
    /*Si se han introducido uno o varios comandos externos...*/
    if (comandoExterno == 1 || linea->ncommands > 1)
    {
        ejecutarComandos(linea);
    }
    pidProcesofg = -1;
    return;
}

/*Implementacion del comado cd para la shell */
void cambiarDirectorio(tcommand comando)
{
    /*Variable que almacena el directorio al que se quiere ir*/
    char *dir;
    /*Variable para almacenar la ruta del directorio devuelta por 'getcwd()'*/
    char ruta[T_MAX_LINEA];

    /*Si se introduce un cd con dos argumentos o mas ...*/
    if (comando.argc > 2)
    {
        /*...se muestra el mensaje de error correspondiente y se sale de la funcion*/
        fprintf(stderr, "Error: numero de argumentos erroneo. Uso 'cd' con %d argumentos\n", comando.argc - 1);
        return;
    }
    /*Si solo se introduce 'cd' se almacena en 'dir' el contenido de $HOME*/
    else if (comando.argc == 1)
    {
        dir = getenv("HOME");
        if (dir == NULL)
        {
            fprintf(stderr, "No existe la variable $HOME\n");
        }
    }
    /*Si se introduce 'cd' con una ruta, se almacena en 'dir' la ruta*/
    else
    {
        dir = comando.argv[1];
    }
    /*Tratamos de cambiar a la ruta del directorio almacenada en 'dir'*/
    if (chdir(dir) != 0)
    {
        /*En caso de error lo notificamos*/
        fprintf(stderr, "Error al cambiar de directorio: %s\n", strerror(errno));
    }
    /*Indicamos al usuario cual el directorio actual, tras todo el proceso*/
    printf("Directorio actual: %s\n", getcwd(ruta, -1));
    return;
}

/*Muestra la lista de procesos que se estan ejecutando en segundo plano*/
void mostrarProcesosbg()
{
    int i;
    for (i = 0; i < nProcesosbg; i++)
    {
        printf("[%d]+ Running", i + 1);
        printf("%*c", 15, ' ');
        printf(" %s", listabg[i].nombre);
    }
    return;
}

/*Funcion que permite eliminar un proceso de 'listabg'*/
int eliminarProcesobg(int pid)
{
    int i, j;

    for (i = 0; i < nProcesosbg; i++)
    {
        if (listabg[i].id == pid)
        {
            for (j = i; j < nProcesosbg - 1; j++)
            {
                listabg[j] = listabg[j + 1];
            }

            nProcesosbg--;

            return 0;
        }
    }
    return 1;
}

/*Funcion que permite agregar un proceso a 'listabg'*/
int agregarProcesobg(pid_t pid, char *linea)
{
    if (nProcesosbg < MAX_PROCESOS_BG)
    {
        listabg[nProcesosbg].id = pid;
        listabg[nProcesosbg].nombre = linea;
        nProcesosbg++;
        return 0;
    }
    else
    {
        printf("Error, no se pueden agregar mas procesos en segundo plano:\n");
        printf("La lista de procesos en segundo plano esta llena\n");
        return 1;
    }
}

void ejecutarEnForeground(tline *linea)
{
    /*Identificador del proceso que se va a tratar de poner en primer plano*/
    pid_t pid;
    /*Numero del proceso ejecutandose en background de 'listabg'*/
    int numProcesobg = 0;

    /*Si se ha especificado el numero del proceso en bg*/
    if (linea->commands[0].argc == 2)
    {
        numProcesobg = atoi(linea->commands[0].argv[1]);
        /*Si el numero del proceso es correcto*/
        if (numProcesobg > 0 && numProcesobg <= nProcesosbg)
        {
            /*Se obtiene el pid del proceso especificado*/
            pid = listabg[numProcesobg - 1].id;
            /*Si se ha conseguido eliminar el proceso de listabg*/
            if (eliminarProcesobg(pid) == 0)
            {
                /*Se actualiza pid del proceso ejecutandose en primer plano*/
                pidProcesofg = pid;
                /*Se espera por la ejecucion del proceso poniendolo asi en primer plano*/
                waitpid(pid, NULL, 0);
            }
            /*Si no se ha encontrado el proceso se notifica con un error */
            else
            {
                fprintf(stderr, "Error no se ha encontrado el proceso %d en la lista de procesos en background\n", numProcesobg);
            }
        }
        /*Si el numero del proceso NO es correcto*/
        else
        {
            fprintf(stderr, "Error no hay ningun proceso con numero %d\n", numProcesobg);
        }
    }
    /*Si se ha puesto 'fg' sin argumentos*/
    else
    {
        /*Si hay algun proceso en background*/
        if (nProcesosbg > 0)
        {
            pid = listabg[nProcesosbg - 1].id;
            /*Si se ha conseguido eliminar el proceso de listabg*/
            if (eliminarProcesobg(pid) == 0)
            {
                /*Se actualiza pid del proceso ejecutandose en primer plano*/
                pidProcesofg = pid;
                /*Se espera por la ejecucion del proceso poniendolo asi en primer plano*/
                waitpid(pid, NULL, 0);
            }
            /*Si no se ha encontrado el proceso se notifica con un error */
            else
            {
                fprintf(stderr, "Error no se ha encontrado el proceso %d en la lista de procesos en background\n", numProcesobg);
            }
        }
        /*Si NO hay ningun proceso en background*/
        else
        {
            printf("No se ha encontrado ningun proceso en background\n");
        }
    }
    return;
}

/*Funcion que redirige la entrada estandar para que sea un fichero*/
void redireccionarEntrada(char *nombreFichero)
{
    int descriptorFichero;

    /*Se trata de abrir el fichero*/
    descriptorFichero = open(nombreFichero, O_RDONLY);
    /*Si se produce un error al abrirlo se notifica al usuario*/
    if (descriptorFichero == -1)
    {
        fprintf(stderr, "%s: Error. %s\n", nombreFichero, strerror(errno));
    }
    /*En el caso de abrirlo con exito se hace la redireccion*/
    else
    {
        if (dup2(descriptorFichero, STDIN_FILENO) == -1)
        {
            fprintf(stderr, "Error al redireccionar la entrada: %s\n", strerror(errno));
        }
    }
    /*Al final de todo el proceso cerramos el fichero*/
    close(descriptorFichero);
    return;
}

/*Funcion que redirige la salida estandar a un fichero (si existe lo trunca)*/
void redireccionarSalida(char *nombreFichero)
{
    int descriptorFichero;

    /*Se trata de abrir el fichero.Si existe se trunca. Si no existe se crea*/
    descriptorFichero = open(nombreFichero, O_WRONLY | O_CREAT | O_TRUNC);
    /*Si se produce un error al abrirlo se notifica al usuario*/
    if (descriptorFichero == -1)
    {
        fprintf(stderr, "%s: Error. %s\n", nombreFichero, strerror(errno));
    }
    /*En el caso de abrirlo con exito se hace la redireccion*/
    else
    {
        if (dup2(descriptorFichero, STDOUT_FILENO) == -1)
        {
            fprintf(stderr, "Error al redireccionar la salida: %s\n", strerror(errno));
        }
    }
    /*Al final de todo el proceso cerramos el fichero*/
    close(descriptorFichero);
    return;
}

/*Funcion que redirige la salida de error*/
void redireccionarError(char *nombreFichero)
{

    int descriptorFichero;

    /*Se trata de abrir el fichero. Si existe se trunca. Si no existe se crea*/
    descriptorFichero = open(nombreFichero, O_WRONLY | O_CREAT | O_TRUNC);
    /*Si se produce un error al abrirlo se notifica al usuario*/
    if (descriptorFichero == -1)
    {
        fprintf(stderr, "%s: Error. %s\n", nombreFichero, strerror(errno));
    }
    /*En el caso de abrirlo con exito se hace la redireccion*/
    else
    {
        if (dup2(descriptorFichero, STDERR_FILENO) == -1)
        {
            fprintf(stderr, "Error al redireccionar salida de error: %s\n", strerror(errno));
        }
    }
    /*Al final de todo el proceso cerramos el fichero*/
    close(descriptorFichero);
    return;
}

void ejecutarComandos(tline *linea)
{
    /*Variable para los bucles for*/
    int i;
    /*Array de punteros a arrays que contendran los 2 descriptores de fichero de cada tuberia*/
    int **dftuberia;

    /*Identificador del proceso de la ejecucion de la linea*/
    int pid_principal;
    /*Identificador del un comando de la linea*/
    int pid;

    char lineaComandos[T_MAX_LINEA];
    char nombreComando[T_MAX_LINEA];

    strcpy(lineaComandos, buffer);
    /* Se crea un nuevo proceso que ejecutara cada comando en un proceso distinto*/
    pid_principal = fork();
    /* Si el valor obtenido en pid es menor que 0 ... */
    if (pid_principal < 0)
    {
        /* se notifica el error al hacer el fork() */
        fprintf(stderr, "Error al crear el proceso principal de la ejecucion de la linea:\n%s\n", strerror(errno));
    }
    /*Proceso hijo: Proceso principal de la ejecucion de la linea*/
    else if (pid_principal == 0)
    {
        /* Los procesos hijos en foreground deben poder acabarse con SIGINT y SIGQUIT */
        if (linea->background)
        {
            signal(SIGINT, SIG_IGN);
            signal(SIGQUIT, SIG_IGN);
        }

        if (linea->ncommands == 1)
        {

            /* Se realizan las redirecciones que hagan falta */
            if (linea->redirect_input != NULL)
                redireccionarEntrada(linea->redirect_input);

            if (linea->redirect_output != NULL)
                redireccionarSalida(linea->redirect_output);

            if (linea->redirect_error != NULL)
                redireccionarError(linea->redirect_error);

            /* Si el mandato existe se trata de ejecutar */
            if (linea->commands[0].filename != NULL)
            {
                execv(linea->commands[0].filename, linea->commands[0].argv);
                /* Si se llega aqui entonces se ha producido un error en el execvp */
                printf("Error al ejecutar el comando: %s\n", strerror(errno));
                return;
            }
            else
            {
                fprintf(stderr, "%s: No se encuentra el mandato\n", linea->commands[0].argv[0]);
                return;
            }
        }
        else
        {

            /*Reservamos memoria para el array de punteros a tuberia y para cada tuberia*/
            dftuberia = (int **)malloc((linea->ncommands - 1) * sizeof(int *));
            for (i = 0; i < linea->ncommands - 1; i++)
            {
                dftuberia[i] = (int *)malloc(2 * sizeof(int *));
                if (pipe(dftuberia[i]) < 0)
                {
                    fprintf(stderr, "Error al crear la tuberia %d:\n%s\n", i, strerror(errno));
                    exit(1);
                }
            }

            /*Para cada comando se crea un proceso que se comunicara ...*/
            /*... con el padre mediente tuberias*/
            for (i = 0; i < linea->ncommands; i++)
            {
                pid = fork();
                /*Si el valor obtenido en pid es menor que 0 ...*/
                if (pid < 0)
                {
                    /*se notifica el error al hacer el fork()*/
                    fprintf(stderr, "Error al iniciar el proceso del comando %d:\n%s\n", i, strerror(errno));
                }
                /*Proceso hijo*/
                else if (pid == 0)
                {
                    if (linea->background)
                    {
                        signal(SIGINT, SIG_IGN);
                        signal(SIGQUIT, SIG_IGN);
                    }

                    if (i < (linea->ncommands - 1))
                    {
                        /*Redirigimos stdout al extremo de escritura del pipe*/
                        close(dftuberia[i][0]);
                        dup2(dftuberia[i][1], STDOUT_FILENO);
                        close(dftuberia[i][1]);
                    }

                    /*Se hacen las redirecciones de fichero que hagan falta*/
                    if (linea->redirect_input != NULL && i == 0)
                        redireccionarEntrada(linea->redirect_input);
                    if (linea->redirect_output != NULL && i == (linea->ncommands - 1))
                        redireccionarSalida(linea->redirect_output);
                    if (linea->redirect_error != NULL && i == (linea->ncommands - 1))
                        redireccionarError(linea->redirect_error);

                    /* Si el mandato existe se trata de ejecutar */
                    if (linea->commands[i].filename != NULL)
                    {
                        execv(linea->commands[i].filename, linea->commands[i].argv);
                        /* Si se llega aqui entonces se ha producido un error en el execvp */
                        printf("Error al ejecutar el comando: %s\n", strerror(errno));
                        exit(1);
                    }
                    else
                    {
                        fprintf(stderr, "%s: No se encuentra el mandato\n", linea->commands[i].argv[0]);
                        exit(1);
                    }
                }
                /*Proceso padre*/
                else
                {
                    if (i < (linea->ncommands - 1))
                    {
                        /*El padre redireccciona la entrada estandar al extremo de lectura del pipe*/
                        close(dftuberia[i][1]);
                        dup2(dftuberia[i][0], STDIN_FILENO);
                        close(dftuberia[i][0]);
                        /*Esperamos por el ultimo proceso*/
                    }

                    strcpy(nombreComando, linea->commands[i].argv[0]);

                    if (linea->background)
                    {
                        /*Se muestra el pid y el nombre del comando que se ejecuta */
                        printf("[%d]+ Running", pid);
                        printf("%*c", 15, ' ');
                        printf(" %s", nombreComando);

                        /* Se añade a la lista de procesos en background */
                        agregarProcesobg(pid, nombreComando);
                    }

                    waitpid(pid, NULL, 0);
                }
            }

            /*Antes de terminar se libera la memoria reservada y se termina el proceso*/
            for (i = 0; i < linea->ncommands - 1; i++)
            {
                free(dftuberia[i]);
            }
        }
        exit(0);
    }
    /*Proceso padre: Proceso del programa principal*/
    else
    {
        /*Si el proceso no se realiza en background...*/
        if (!linea->background)
        {
            /*Se actualiza pid del proceso ejecutandose en primer plano*/
            pidProcesofg = pid_principal;
            /*...el padre espera por la terminacion del hijo*/
            waitpid(pid_principal, NULL, 0);
            /*Se indica que ya no hay ningun proceso en foreground*/
            pidProcesofg = -1;
        }
        /* Si el proceso se ejecuta en background... */
        else
        {
            /*se muestra el pid_principal y el nombre de la linea que se ejecuta*/
            printf("[%d]+ Running", pid_principal);
            printf("%*c", 15, ' ');
            printf(" %s", lineaComandos);

            /*y se añade a la lista de procesos en background*/
            agregarProcesobg(pid_principal, lineaComandos);
        }

        return;
    }
}
