/*
 * casio_system.c
 * Programa orquestador del sistema de calendarización CASIO.
 * Lee la configuración de tareas desde un archivo, lanza múltiples
 * procesos casio_task como hijos usando fork/execv, y controla su
 * inicio y fin mediante señales SIGUSR1 y SIGUSR2.
 *
 * Autor: Paulo Baltarejo Sousa y Luis Lino Ferreira
 * Laboratorio 4 - CC3064 Sistemas Operativos - UVG 2026
 */

#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <time.h>

/* Tamaño máximo del buffer para leer líneas del archivo de configuración */
#define BUF_LEN         200
/* Número máximo de tareas CASIO que puede manejar el sistema */
#define CASIO_TASKS_NUM 20

/*
 * casio_tasks_config: estructura que almacena los parámetros de configuración
 * de cada tarea CASIO leídos desde el archivo de sistema.
 * - pid: identificador de la tarea (casio_id)
 * - min_exec/max_exec: rango de tiempo de ejecución de cada job (segundos)
 * - min/max_inter_arrival: rango de tiempo entre llegadas de jobs (segundos)
 * - deadline: plazo de ejecución (segundos)
 * - min/max_offset: rango del offset inicial antes del primer job (segundos)
 */
struct casio_tasks_config {
    int pid;
    double min_exec;
    double max_exec;
    double min_inter_arrival;
    double max_inter_arrival;
    double deadline;
    double min_offset;
    double max_offset;
};

/* Array global de PIDs de los procesos hijos (casio_tasks) lanzados */
pid_t casio_tasks_pid[CASIO_TASKS_NUM];
/* Número actual de tareas CASIO configuradas */
int casio_tasks_num = 0;

/* get_int_val: extrae un valor entero de una cadena delimitada por tabulaciones.
 * Modifica la cadena original insertando '\0' en el primer tab encontrado. */
int get_int_val(char* str)
{
    char* s = str;
    int val;
    for(s = str; *s != '\t'; s++);
    *s = '\0';
    val = atoi(str);
    return val;
}

/* print_casio_tasks_config: imprime la configuración de todas las tareas
 * en formato tabular para verificación y depuración. */
void print_casio_tasks_config(struct casio_tasks_config *tasks, int num)
{
    int i;
    printf("\nCASIO TASKS CONFIG\n");
    printf("pid\tmin_c\tmax_c\tmin_t\tmax_t\tdeadl\tmin_o\tmax_o\n");
    for(i = 0; i < num; i++) {
        printf("%d\t%f\t%f\t%f\t%f\t%f\t%f\t%f\n",
            tasks[i].pid,
            tasks[i].min_exec,
            tasks[i].max_exec,
            tasks[i].min_inter_arrival,
            tasks[i].max_inter_arrival,
            tasks[i].deadline,
            tasks[i].min_offset,
            tasks[i].max_offset);
    }
}

/* clear_casio_tasks_config_info: inicializa todos los campos de las
 * configuraciones de tareas a cero antes de leer el archivo. */
void clear_casio_tasks_config_info(struct casio_tasks_config *tasks, int num)
{
    int i;
    for(i = 0; i < num; i++) {
        tasks[i].pid = 0;
        tasks[i].min_exec = 0;
        tasks[i].max_exec = 0;
        tasks[i].min_inter_arrival = 0;
        tasks[i].max_inter_arrival = 0;
        tasks[i].deadline = 0;
        tasks[i].min_offset = 0;
        tasks[i].max_offset = 0;
    }
}

/* get_casio_task_config_info: parsea una línea del archivo de configuración
 * que contiene los 8 parámetros de una tarea separados por tabulaciones.
 * Incrementa el contador *n al terminar de parsear una tarea. */
void get_casio_task_config_info(char *str, struct casio_tasks_config *tasks, int *n)
{
    char *s, *s1;
    int i = 0;
    s = s1 = str;

    /* Recorre la cadena caracter a caracter buscando tabulaciones como delimitadores */
    while(i < 7) {
        if(*s == '\t') {
            *s = '\0'; /* Reemplaza el tab con fin de cadena para usar atoi/atof */
            switch(i) {
                case 0: tasks[*n].pid = atoi(s1); s1 = s+1; i++; break;
                case 1: tasks[*n].min_exec = atof(s1); s1 = s+1; i++; break;
                case 2: tasks[*n].max_exec = atof(s1); s1 = s+1; i++; break;
                case 3: tasks[*n].min_inter_arrival = atof(s1); s1 = s+1; i++; break;
                case 4: tasks[*n].max_inter_arrival = atof(s1); s1 = s+1; i++; break;
                case 5: tasks[*n].deadline = atof(s1); s1 = s+1; i++; break;
                case 6:
                    tasks[*n].min_offset = atof(s1);
                    s1 = s+1;
                    i++;
                    tasks[*n].max_offset = atof(s1); /* El último campo no tiene tab */
                    break;
            }
        }
        s++;
    }
    (*n)++; /* Incrementa el contador de tareas parseadas */
}

/* get_casio_tasks_config_info: lee y parsea el archivo de configuración completo.
 * La primera línea contiene la duración de la simulación.
 * Las líneas siguientes contienen la configuración de cada tarea CASIO.
 * El archivo 'system' contiene esta información en formato tabulado. */
void get_casio_tasks_config_info(char *file, int *duration,
    struct casio_tasks_config *tasks, int *n)
{
    char buffer[BUF_LEN];
    int count = 0;
    FILE* fd = fopen(file, "r");
    *n = 0;
    buffer[0] = '\0';

    while((fgets(buffer, BUF_LEN, fd)) != NULL) {
        if(strlen(buffer) > 1) {
            switch(count) {
                case 0:
                    /* Primera línea: duración total de la simulación en segundos */
                    *duration = get_int_val(buffer);
                    count++;
                    break;
                default:
                    /* Líneas siguientes: parámetros de cada tarea CASIO */
                    get_casio_task_config_info(buffer, tasks, n);
            }
        }
        buffer[0] = '\0';
    }
    fclose(fd);
}

/* start_simulation: envía SIGUSR1 a todos los procesos casio_task para
 * iniciarlos simultáneamente. Representa el tiempo cero de la simulación. */
void start_simulation()
{
    int i;
    printf("I will send a SIGUSR1 signal to start all tasks\n");
    for(i = 0; i < casio_tasks_num; i++) {
        kill(casio_tasks_pid[i], SIGUSR1);
    }
}

/* end_simulation: manejador de SIGALRM. Se invoca cuando expira el timer
 * de duración de la simulación. Envía SIGUSR2 a todos los procesos hijos
 * para que terminen ordenadamente. */
void end_simulation(int signal)
{
    int i;
    printf("I will send a SIGUSR2 signal to finish all tasks\n");
    for(i = 0; i < casio_tasks_num; i++) {
        kill(casio_tasks_pid[i], SIGUSR2);
    }
}

/* help: imprime el uso correcto del programa y termina. */
void help(char* name)
{
    fprintf(stderr, "Usage: %s file_name (system configuration)\n", name);
    exit(0);
}

/*
 * main: punto de entrada de casio_system.
 * Recibe como argumento el nombre del archivo de configuración (ej: "system").
 * 
 * Flujo principal:
 * 1. Lee la configuración de tareas del archivo
 * 2. Configura un timer para la duración total de la simulación
 * 3. Por cada tarea: hace fork() y execv() para lanzar casio_task
 * 4. Envía SIGUSR1 a todas las tareas para iniciarlas (tiempo cero)
 * 5. Espera con pause() hasta que el timer de simulación expire (SIGALRM)
 * 6. end_simulation envía SIGUSR2 a todas las tareas para terminarlas
 * 7. Espera a que todos los hijos terminen con wait()
 */
int main(int argc, char *argv[])
{
    int duration, i, j, k, n;
    struct casio_tasks_config casio_tasks_config[CASIO_TASKS_NUM];

    /* itimerval: estructura de Linux para timers de intervalo.
     * it_value: tiempo hasta el primer disparo.
     * it_interval: tiempo entre disparos repetidos (0 = una sola vez).
     * Al expirar envía SIGALRM al proceso. */
    struct itimerval sim_time;

    /* Buffers para los argumentos que se pasarán a casio_task via execv */
    char arg[CASIO_TASKS_NUM][BUF_LEN], *parg[CASIO_TASKS_NUM];

    srand(time(NULL)); /* Inicializar generador aleatorio */

    if(argc != 2) {
        help(argv[0]);
    }

    /* Limpiar e inicializar las configuraciones de tareas */
    clear_casio_tasks_config_info(casio_tasks_config, CASIO_TASKS_NUM);

    /* Leer configuración desde el archivo especificado como argumento */
    get_casio_tasks_config_info(argv[1], &duration, casio_tasks_config, &casio_tasks_num);

    /* Configurar el timer de duración total de la simulación.
     * Cuando expire, enviará SIGALRM que invocará end_simulation. */
    sim_time.it_interval.tv_sec = 0;
    sim_time.it_interval.tv_usec = 0;
    sim_time.it_value.tv_sec = duration;
    sim_time.it_value.tv_usec = 0;

    signal(SIGALRM, end_simulation); /* Registrar manejador de fin de simulación */
    setitimer(ITIMER_REAL, &sim_time, NULL); /* Iniciar timer de simulación */

    /* Lanzar cada tarea CASIO como proceso hijo */
    for(i = 0; i < casio_tasks_num; i++) {

        /* Preparar argumentos para casio_task:
         * nombre del ejecutable + 9 parámetros de configuración */
        strcpy(arg[0], "casio_task");
        sprintf(arg[1], "%d", casio_tasks_config[i].pid);
        sprintf(arg[2], "%f", casio_tasks_config[i].min_exec);
        sprintf(arg[3], "%f", casio_tasks_config[i].max_exec);
        sprintf(arg[4], "%f", casio_tasks_config[i].min_inter_arrival);
        sprintf(arg[5], "%f", casio_tasks_config[i].max_inter_arrival);
        sprintf(arg[6], "%f", casio_tasks_config[i].deadline);
        sprintf(arg[7], "%f", casio_tasks_config[i].min_offset);
        sprintf(arg[8], "%f", casio_tasks_config[i].max_offset);
        sprintf(arg[9], "%ld", rand()); /* Semilla aleatoria única para cada tarea */
        n = 10;

        /* Construir array de punteros a argumentos requerido por execv */
        for(k = 0; k < n; k++) {
            parg[k] = arg[k];
        }
        parg[k] = NULL; /* execv requiere NULL como último elemento */

        /* fork(): crea un proceso hijo.
         * En el hijo (pid==0): execv reemplaza la imagen del proceso con casio_task.
         * En el padre: guarda el PID del hijo para enviarle señales después. */
        casio_tasks_pid[i] = fork();
        if(casio_tasks_pid[i] == 0) {
            execv("./casio_task", parg); /* Reemplaza proceso hijo con casio_task */
            perror("Error: execv\n");   /* Solo se ejecuta si execv falla */
            exit(0);
        }

        sleep(1); /* Esperar 1 segundo entre lanzamientos para dar tiempo al hijo */
    }

    start_simulation(); /* Tiempo cero: enviar SIGUSR1 a todas las tareas */

    /* pause(): suspende el proceso padre hasta recibir una señal.
     * Aquí espera hasta que el timer de simulación dispare SIGALRM,
     * lo que invocará end_simulation. */
    pause();

    /* Esperar a que todos los procesos hijos terminen antes de salir */
    for(i = 0; i < casio_tasks_num; i++) {
        wait(NULL);
    }

    printf("All tasks have finished properly!!!\n");
    return 0;
}
