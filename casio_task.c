/*
 * casio_task.c
 * Representa una tarea individual calendarizada por la política CASIO (EDF).
 * Cada tarea recibe parámetros por línea de comandos, se registra con el
 * scheduler CASIO mediante sched_setscheduler, y ejecuta "jobs" periódicos
 * controlados por señales y timers.
 *
 * Autor: Paulo Baltarejo Sousa y Luis Lino Ferreira
 * Laboratorio 4 - CC3064 Sistemas Operativos - UVG 2026
 */

#include <sys/time.h>
#include <stdlib.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>

/* LOOP_ITERATIONS_PER_MILLISEC: número de iteraciones del loop vacío que
 * equivalen aproximadamente a 1 millisegundo de CPU. Este valor es
 * dependiente del hardware — debe ajustarse según la velocidad del procesador. */
#define LOOP_ITERATIONS_PER_MILLISEC 178250

/* Constantes de conversión de unidades de tiempo */
#define MILLISEC    1000        /* milisegundos en un segundo */
#define MICROSEC    1000000     /* microsegundos en un segundo */
#define NANOSEC     1000000000  /* nanosegundos en un segundo */

/*
 * Nota sobre sched_param modificado:
 * En linux-2.6.24-casio/include/linux/sched.h y en /usr/include/bits/sched.h
 * se agregaron los campos casio_id y deadline a la estructura sched_param
 * para soportar la política CASIO. casio_id identifica la tarea dentro del
 * scheduler, y deadline es el plazo de ejecución en nanosegundos usado por EDF.
 */

/* Variables globales que definen los rangos de tiempo de la tarea */
double min_offset, max_offset;                          /* offset inicial antes del primer job (segundos) */
double min_exec_time, max_exec_time;                    /* tiempo de ejecución de cada job (segundos) */
double min_inter_arrival_time, max_inter_arrival_time;  /* tiempo entre llegadas de jobs (segundos) */

/* Identificador de la tarea CASIO y contador de jobs ejecutados */
unsigned int casio_id, jid = 1;

/* Timer que controla el intervalo entre llegadas de jobs usando SIGALRM */
struct itimerval inter_arrival_time;


/* burn_1millisecs: ejecuta un loop vacío que consume aproximadamente 1ms de CPU.
 * Se usa para simular carga de trabajo sin llamadas al sistema. */
void burn_1millisecs() {
    unsigned long long i;
    for(i = 0; i < LOOP_ITERATIONS_PER_MILLISEC; i++);
}

/* burn_cpu: consume la CPU durante 'milliseconds' milisegundos
 * llamando repetidamente a burn_1millisecs. */
void burn_cpu(long milliseconds) {
    long i;
    for(i = 0; i < milliseconds; i++)
        burn_1millisecs();
}

/* clear_sched_param: inicializa los campos CASIO de sched_param con valores
 * por defecto (-1 para casio_id, 0 para deadline). */
void clear_sched_param(struct sched_param *param)
{
    param->casio_id = -1;
    param->deadline = 0;
}

/* print_task_param: imprime el casio_id y deadline de la tarea.
 * Útil para verificar los parámetros antes y después de sched_setscheduler. */
void print_task_param(struct sched_param *param)
{
    printf("\npid[%d]\n", param->casio_id);
    printf("deadline[%llu]\n", param->deadline);
}

/* clear_signal_timer: pone a cero todos los campos de un itimerval,
 * desactivando efectivamente el timer. */
void clear_signal_timer(struct itimerval *t)
{
    t->it_interval.tv_sec = 0;
    t->it_interval.tv_usec = 0;
    t->it_value.tv_sec = 0;
    t->it_value.tv_usec = 0;
}

/* set_signal_timer: configura un itimerval para disparar una sola vez
 * después de 'secs' segundos. Convierte segundos a tv_sec y tv_usec.
 * itimerval es una estructura de Linux para timers de intervalo que
 * envían SIGALRM cuando expiran. */
void set_signal_timer(struct itimerval *t, double secs)
{
    t->it_interval.tv_sec = 0;
    t->it_interval.tv_usec = 0;
    t->it_value.tv_sec = (int)secs;
    t->it_value.tv_usec = (secs - t->it_value.tv_sec) * MICROSEC;
}

/* print_signal_timer: imprime los campos del timer para depuración. */
void print_signal_timer(struct itimerval *t)
{
    printf("Interval: secs [%ld] usecs [%ld] Value: secs [%ld] usecs [%ld]\n",
        t->it_interval.tv_sec,
        t->it_interval.tv_usec,
        t->it_value.tv_sec,
        t->it_value.tv_usec);
}

/* get_time_value: retorna un valor aleatorio uniforme entre min y max.
 * Si min == max retorna ese valor directamente. Se usa para simular
 * variabilidad en tiempos de ejecución e inter-llegada. */
double get_time_value(double min, double max)
{
    if(min == max)
        return min;
    return (min + (((double)rand() / RAND_MAX) * (max - min)));
}

/* start_task: manejador de señal SIGUSR1. Es invocado por casio_system
 * para iniciar la tarea. Imprime el inicio y configura el primer timer
 * con un offset aleatorio antes del primer job. */
void start_task(int s)
{
    printf("\nTask(%d) has just started\n", casio_id);
    /* Configura el timer con un offset inicial aleatorio */
    set_signal_timer(&inter_arrival_time, get_time_value(min_offset, max_offset));
    setitimer(ITIMER_REAL, &inter_arrival_time, NULL);
}

/* do_work: manejador de señal SIGALRM. Se ejecuta cada vez que el timer
 * de inter-llegada expira. Simula la ejecución de un job quemando CPU,
 * luego reprograma el timer para el siguiente job. */
void do_work(int s)
{
    signal(SIGALRM, do_work); /* Reinstala el manejador para el siguiente SIGALRM */

    /* Programa el siguiente job con tiempo de inter-llegada aleatorio */
    set_signal_timer(&inter_arrival_time,
        get_time_value(min_inter_arrival_time, max_inter_arrival_time));
    setitimer(ITIMER_REAL, &inter_arrival_time, NULL);

    clock_t start, end;
    double elapsed = 0;
    start = clock();
    printf("Job(%d,%d) starts\n", casio_id, jid);

    /* Simula trabajo quemando CPU por un tiempo aleatorio */
    burn_cpu(get_time_value(min_exec_time, max_exec_time) * MILLISEC);

    end = clock();
    elapsed = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("Job(%d,%d) ends (%f)\n", casio_id, jid, elapsed);
    jid++; /* Incrementa el contador de jobs */
}

/* end_task: manejador de señal SIGUSR2. Es invocado por casio_system
 * para terminar la tarea. Imprime el fin y llama a exit(). */
void end_task(int s)
{
    printf("\nTask(%d) has finished\n", casio_id);
    exit(0);
}

/*
 * main: punto de entrada de casio_task.
 * Recibe 9 argumentos por línea de comandos (pasados por casio_system):
 *   argv[1]: casio_id (identificador de la tarea)
 *   argv[2]: min_exec_time (tiempo mínimo de ejecución en segundos)
 *   argv[3]: max_exec_time (tiempo máximo de ejecución en segundos)
 *   argv[4]: min_inter_arrival_time (tiempo mínimo entre jobs)
 *   argv[5]: max_inter_arrival_time (tiempo máximo entre jobs)
 *   argv[6]: deadline (plazo en segundos, convertido a nanosegundos)
 *   argv[7]: min_offset (offset mínimo antes del primer job)
 *   argv[8]: max_offset (offset máximo antes del primer job)
 *   argv[9]: seed (semilla para el generador de números aleatorios)
 */
int main(int argc, char** argv) {

    struct sched_param param;
    unsigned long long seed;
    int i;

    /* Inicializar timer y parámetros del scheduler */
    clear_signal_timer(&inter_arrival_time);
    clear_sched_param(&param);
    param.sched_priority = 1;

    /* Leer parámetros de la línea de comandos */
    casio_id = param.casio_id = atoi(argv[1]);
    min_exec_time = atof(argv[2]);
    max_exec_time = atof(argv[3]);
    min_inter_arrival_time = atof(argv[4]);
    max_inter_arrival_time = atof(argv[5]);
    param.deadline = atof(argv[6]) * NANOSEC; /* Convertir deadline a nanosegundos */
    min_offset = atof(argv[7]);
    max_offset = atof(argv[8]);
    seed = atol(argv[9]);
    srand(seed); /* Inicializar generador aleatorio con semilla única por tarea */

    /* Registrar manejadores de señales:
     * SIGUSR1 -> start_task (inicio de la tarea)
     * SIGALRM -> do_work (ejecución de jobs periódicos)
     * SIGUSR2 -> end_task (fin de la tarea) */
    signal(SIGUSR1, start_task);
    signal(SIGALRM, do_work);
    signal(SIGUSR2, end_task);

    print_task_param(&param);
    printf("Before sched_setscheduler:priority %d\n", sched_getscheduler(0));

    /* Cambiar la política de calendarización a SCHED_CASIO (EDF).
     * Esto registra la tarea en el scheduler CASIO del kernel con
     * su casio_id y deadline configurados. */
    if(sched_setscheduler(0, SCHED_CASIO, &param) == -1) {
        perror("ERROR");
    }

    printf("After sched_setscheduler:priority %d\n", sched_getscheduler(0));

    /* Esperar señales indefinidamente mientras la tarea está idle.
     * pause() suspende el proceso hasta recibir una señal. */
    while(1) {
        pause();
    }

    return 0;
}
