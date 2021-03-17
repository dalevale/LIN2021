#include <pthread.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define MAX_BUF_SIZE	20		/* Para el buffer del hilo de lectura */
#define ENTRY_NAME_SIZE	10		/* Tamaño máximo de la entrada /proc que crear */
#define ARRAY_SIZE		8		/* Tamaño del vector que utiliza el hilo de escritura */
#define TYPE_STR		0 		/* Tipo cadena de carácteres */
#define TYPE_INT		1		/* Tipo numeros enteros */
#define COMMAND_SIZE	18		/* Tamaño del buffer que se utiliza para introducir commandos en la entrada /proc/multipc/admin */
#define INITAL_ARGS		4		/* Argumentos iniciales del programa */
#define PAIR_ARGS		2		/* Argumentos de tipo -r/-w <delay> */
#define	OPTION_ARG		1		/* Indíce del argumento <option> */
#define	PROC_NAME_ARG	2		/* Indíce del argumento <proc_name_entry> */
#define	TYPE_ARG		3		/* Indíce del argumento <type> */
#define MAX_THREADS		6		/* Número máximo de hilos que se pueden crear */

extern int errno;

char *proc_dir_admin = "/proc/multipc/admin";
char *proc_dir = "/proc/multipc/";
char *command1 = "new";
char *command2 = "delete";

struct resources{
	char name[ENTRY_NAME_SIZE];
	int type;
	int delay;
	int thread_num;
};


void* reader(void *res){
	struct resources *r = (struct resources *) res;
	char proc_file[strlen(proc_dir) + ENTRY_NAME_SIZE];
	int delay = r->delay;
	int thread_num = r->thread_num;

	int fd, numbytes;
	char buf[MAX_BUF_SIZE];

	sprintf(proc_file, "%s%s", proc_dir, r->name);
	while(1){
		fd = open(proc_file, O_RDONLY);
		
		if(fd == -1){
			fprintf(stderr, "Error al abrir el fichero %s.\n", proc_file);
			_exit(1);
		}

		numbytes = read(fd, buf, MAX_BUF_SIZE);
		buf[numbytes] = '\0';
		printf("Thread %d: %s", thread_num, buf);

		sleep(delay);
		close(fd);
	}
}


void* writer(void *res){
	
	struct resources *r = (struct resources *) res;
	char proc_file[strlen(proc_dir) + ENTRY_NAME_SIZE];
	int delay = r->delay;
	int type = r->type;

	char *ciudades[ARRAY_SIZE] = {"Madrid", "Berlin", "Paris", "Nueva York", "Londres", "Roma", "Amsterdam", "Viena"};
	char *num[ARRAY_SIZE] = {"1", "2", "3", "4", "5", "6", "7", "8"};
	int fd, i = 0, ret = 0;;

	sprintf(proc_file, "%s%s", proc_dir, r->name);
	while(1){

		fd = open(proc_file, O_WRONLY);
		
		if(fd == -1){
			fprintf(stderr, "Error al abrir el fichero %s.\n", proc_file);
			_exit(1); 
		} else if(type == TYPE_INT)
			ret = write(fd, num[i], strlen(num[i])+1);
		else if (type == TYPE_STR)
			ret = write(fd, ciudades[i], strlen(ciudades[i])+1);
		
		if (ret==-1)
			fprintf(stderr, "%s\n", strerror(errno));	

		i++;
		i %= ARRAY_SIZE;

		sleep(delay);
		close(fd);
	}
}


void handle_error(){	
	fprintf(stderr, "./user <options -n/-e/-d> <proc_entry_name> <type s/i> [<-r/-w> <segundos>] ...\n");
	fprintf(stderr, "See README for more info.\n");
	_exit(1);
}


int main(int argc, char **argv){	

	int numbytes, fd;
	char *buf=NULL;
	int num_threads, arg;

	if(argc < INITAL_ARGS - 1)
		handle_error();

	if(!strcmp(argv[OPTION_ARG], "-n")){

		printf("Creating %s entry of type %s.\n", argv[PROC_NAME_ARG], argv[TYPE_ARG]);	
		fd = open(proc_dir_admin, O_WRONLY);
		
		if(fd == -1){
			fprintf(stderr, "%s file doesn't exist.\n", proc_dir_admin);
			_exit(1);
		}
		
		buf = malloc(COMMAND_SIZE);
		numbytes = sprintf(buf, "%s %s %s", command1, argv[PROC_NAME_ARG], argv[TYPE_ARG]);
		
		if(write(fd, buf, numbytes)== -1){
			if(errno == ENOMEM){
				fprintf(stderr, "Proc entry name \"%s\" already exists.\n", argv[PROC_NAME_ARG]);
				_exit(1);
			} else
				handle_error();
		}

		printf("%s entry created.\n", argv[PROC_NAME_ARG]);
		close(fd);

	} else if (!strcmp(argv[OPTION_ARG], "-e")){

		if(((argc - INITAL_ARGS) % PAIR_ARGS)!= 0 || argc == INITAL_ARGS)
			handle_error();
		
		num_threads = (argc - INITAL_ARGS) / PAIR_ARGS;
		if(num_threads > MAX_THREADS){
			fprintf(stderr, "Cannot create more than %d threads.\n", MAX_THREADS);
			_exit(1);
		}

		pthread_t t[num_threads];
		struct resources r[num_threads];

		for(int i = 0; i < num_threads; i++) {

			arg = (i*PAIR_ARGS)+INITAL_ARGS;
			sprintf(r[i].name, argv[PROC_NAME_ARG]);
			r[i].thread_num = i;

			if(!strcmp(argv[TYPE_ARG], "s"))
				r[i].type = TYPE_STR;
			else if (!strcmp(argv[TYPE_ARG], "i"))
				r[i].type = TYPE_INT;
			else
				handle_error();
			
			if(sscanf(argv[arg + 1], "%d", &r[i].delay) !=1)
				handle_error();
			
			if(!strcmp(argv[arg], "-w")){
				printf("Thread %d created for writing.\n", i);
				pthread_create(&t[i], NULL, writer, (void *)&r[i]);
			} else if (!strcmp(argv[arg], "-r")){
				printf("Thread %d created for reading.\n", i);
				pthread_create(&t[i], NULL, reader, (void *)&r[i]);
			} else
				handle_error();
		}

		for(int i = 0; i < num_threads; i++)
			pthread_join(t[i], NULL);
	} else if (!strcmp(argv[OPTION_ARG], "-d")){
		
		printf("Deleting %s entry.\n", argv[PROC_NAME_ARG]);	
		fd = open(proc_dir_admin, O_WRONLY);
		
		if(fd == -1){
			fprintf(stderr, "%s file doesn't exist.\n", proc_dir_admin);
			_exit(1);
		}
		
		buf = malloc(COMMAND_SIZE);
		numbytes = sprintf(buf, "%s %s", command2, argv[PROC_NAME_ARG]);
		
		if(write(fd, buf, numbytes)== -1)
			handle_error();
		
		printf("%s entry deleted.\n", argv[PROC_NAME_ARG]);
		close(fd);
	}
	else
		handle_error();

	return 0;
}