#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include "parmake.h"
#include "parser.h"
#include "queue.h"
#include "rule.h"

#define MAKE1 "makefile"
#define MAKE2 "Makefile"
#define DEFAULT_THREADS (1)

typedef struct _thread_argument_t {
	int thread_id;
	void *parameter;
} thread_argument_t;

typedef struct _thread_pool_t {
	int threads;
	pthread_t *tid;
	thread_argument_t *arg;
} thread_pool_t;

typedef enum {
  false = 0,
  true = 1
} boolean_t;

typedef struct _file_pair_data_t {
	struct stat a;
	struct stat b;
} file_pair_data_t;

void thread_pool_init(thread_pool_t *p, int size, void *(*start_routine) (void *));
void thread_pool_wait(thread_pool_t *p);
void thread_pool_destory(thread_pool_t *p);
void *start(void *argument);
void argument_parse(int argc, char**argv);
void exit_cleanup(const int exit_num);
void parse_makefile();
void parsed_new_target(char *target);
void parsed_new_dependency(char *target, char *dependency);
void parsed_new_command(char *target, char *command);
double modification_time(char *file_name);
void process(rule_t *r, int thread_id);
rule_t *search_queue(queue_t *q, char *target);
void run(rule_t *r);
boolean_t is_dependent(rule_t *r);
boolean_t is_rule(char *target);
boolean_t is_satisfied(char *target);
boolean_t is_file(char *target);
boolean_t is_file_dependent(rule_t *r);

pthread_mutex_t m_task_queue = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m_completed_tasks = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cv_wait = PTHREAD_COND_INITIALIZER;
pthread_cond_t writer_cv = PTHREAD_COND_INITIALIZER;
pthread_cond_t reader_cv = PTHREAD_COND_INITIALIZER;

extern char *optarg;
extern int optind;
extern int opterr;
char *makefile;
int threads;
char **targets;
size_t num_targets;
//implementation from https://en.wikipedia.org/wiki/Thread_pool
thread_pool_t thread_pool;
queue_t task_queue;
queue_t completed_tasks;
queue_t task_history;
file_pair_data_t *file_stat;
int writers, writing, readers, reading;

// Treat this as main
int parmake(int argc, char **argv) {
	// good luck!
	argument_parse(argc, argv);
	parse_makefile();
	thread_pool_init(&thread_pool, threads, start);
	thread_pool_wait(&thread_pool);
	thread_pool_destory(&thread_pool);
	exit_cleanup(EXIT_SUCCESS);
	return 0;
}

void thread_pool_init(thread_pool_t *p, int size, void *(*start_routine) (void *)) {
	p->threads = size;
	p->tid = malloc(sizeof(pthread_t) * p->threads);
	p->arg = malloc(sizeof(thread_argument_t) * p->threads);
	int i = 0;
	for(i = 0; i < p->threads; i++) {
		p->arg[i].thread_id = i;
		pthread_create(p->tid + i, NULL, start_routine, (void *)(p->arg + i));
	}
}

void thread_pool_wait(thread_pool_t *p) {
	int i = 0;
	for(i = 0; i < p->threads; i++) {
		pthread_join(thread_pool.tid[i], NULL);
	}
}

void thread_pool_destory(thread_pool_t *p) {
	free(p->tid);
	free(p->arg);
}

void *start(void *argument) {
	thread_argument_t *arg = (thread_argument_t *)argument;
	size_t i = 0, q_size = 0;
	rule_t *r = NULL; boolean_t found = false;
	while(1) {
		i = 0; r = NULL; found = false;
		pthread_mutex_lock(&m_task_queue);
		if((q_size = queue_size(&task_queue)) == 0) {
			pthread_mutex_unlock(&m_task_queue);
			return NULL;
		}
		for(i = 0; i < q_size; i++) { //try to find best rule first
			r = queue_at(&task_queue, i);
			if(!is_dependent(r)) {
				queue_remove_at(&task_queue, i);
				found = true;
				break;
			}
		}
		if(!found) {
			r = queue_dequeue(&task_queue);
			while(is_dependent(r)) {
				pthread_cond_wait(&cv_wait, &m_task_queue);
			}
		}
		pthread_mutex_unlock(&m_task_queue);
		process(r, arg->thread_id);
		//writers critical section;
		pthread_mutex_lock(&m_completed_tasks);
		writers++;
		while(writing || reading) {
			pthread_cond_wait(&writer_cv, &m_completed_tasks);
		}
		writing++;
		pthread_mutex_unlock(&m_completed_tasks);
		//writing
		queue_enqueue(&completed_tasks, r);
		pthread_cond_broadcast(&cv_wait);
		//writing
		pthread_mutex_lock(&m_completed_tasks);
		writing--;
		writers--;
		if(writers) {
			pthread_cond_signal(&writer_cv);
		} else if(readers) {
			pthread_cond_broadcast(&reader_cv);
		}
		pthread_mutex_unlock(&m_completed_tasks);
	}
	return NULL;
}

void argument_parse(int argc, char**argv) {
	opterr = 0;
	int c; boolean_t f_flag = false, j_flag = false;
	while((c = getopt(argc, argv, "f:j:")) != -1) {
		switch(c) {
			case 'f':
				f_flag = true;
				makefile = optarg;
				break;
			case 'j':
				j_flag = true;
				threads = atoi(optarg);
				break;
			default:
				//perror("parmake: incorrect arguments\n");
				exit_cleanup(EXIT_FAILURE);
				break;
		}
	}
	if(f_flag) {
		if(access(makefile, R_OK) == -1) {
			//perror("parmake: cannot open makefile\n");
			exit_cleanup(EXIT_FAILURE);
		}
	} else {
		if(access(MAKE1, R_OK) == 0) {
			makefile = MAKE1;
		} else if(access(MAKE2, R_OK) == 0) {
			makefile = MAKE2;
		} else {
			//perror("parkmake: cannot find makefile\n");
			exit_cleanup(EXIT_FAILURE);
		}
	}
	if(!j_flag) {
		threads = DEFAULT_THREADS;
	}
	if(optind < argc) {
		num_targets = argc - optind;
		targets = malloc(sizeof(char *) * (num_targets + 1));
		size_t i = 0;
		for(i = 0; i < num_targets; i++) {
			targets[i] = strdup(argv[optind++]);
		}
		targets[i] = NULL;
	}
}

void exit_cleanup(const int exit_num) {
	size_t i = 0; rule_t *r = NULL; char *str = NULL;
	for(i = 0; i < num_targets; i++) {
		free(targets[i]);
	}
	free(targets);
	free(file_stat);
	queue_destroy(&task_queue);
	queue_destroy(&completed_tasks);
	while((r = queue_dequeue(&task_history)) != NULL) {
		free(r->target);
		while((str = queue_dequeue(r->deps)) != NULL) {
			free(str);
		}
		while((str = queue_dequeue(r->commands)) != NULL) {
			free(str);
		}
		rule_destroy(r);
		free(r);
	}
	queue_destroy(&task_history);
	if(exit_num == 0) {
		exit(EXIT_SUCCESS);
	} else {
		exit(EXIT_FAILURE);
	}
}

void parse_makefile() {
	file_stat = malloc(sizeof(file_pair_data_t) * threads);
	queue_init(&task_queue);
	queue_init(&completed_tasks);
	queue_init(&task_history);
	parser_parse_makefile(makefile, targets, parsed_new_target, parsed_new_dependency, parsed_new_command);
}

void parsed_new_target(char *target) {
	rule_t *r = malloc(sizeof(rule_t));
	rule_init(r);
	r->target = strdup(target);
	queue_enqueue(&task_queue, r);
	queue_enqueue(&task_history, r);
}

void parsed_new_dependency(char *target, char *dependency) {
	rule_t *r = search_queue(&task_queue, target);
	if(r != NULL) {
		char *d = strdup(dependency);
		queue_enqueue(r->deps, d);
	}
}

void parsed_new_command(char *target, char *command) {
	rule_t *r = search_queue(&task_queue, target);
	if(r != NULL) {
		char *c = strdup(command);
		queue_enqueue(r->commands, c);
	}
}

double modification_time(char *file_name) {
	struct stat s;
	if(stat(file_name, &s) == -1) {
		//perror("parmake: cannot find file stat\n");
		exit_cleanup(EXIT_FAILURE);
	}
	return difftime(s.st_mtime, 0);
}

void process(rule_t *r, int thread_id) {
	if(is_file(r->target)) {
		if(is_file_dependent(r)) {
			run(r);
		} else {
			queue_t *q = r->deps;
			size_t i; char *target;
			struct stat *a = &file_stat[thread_id].a;
			struct stat *b = &file_stat[thread_id].b;
			for(i = 0; i < queue_size(q); i++) {
				target = queue_at(q, i);
				stat(target, a);
				stat(r->target, b);
				if(difftime(a->st_mtime, b->st_mtime) >= 0) {
					run(r);
					return;
				}
			}
		}
	} else {
		run(r);
	}
}

//search a queue and finds a rule with target name = target
rule_t *search_queue(queue_t *q, char *target) {
	size_t i; rule_t *r;
	for(i = 0; i < queue_size(q); i++) {
		r = queue_at(q, i);
		if(strcmp(r->target, target) == 0) {
			return r;
		}
	}
	return NULL;
}

void run(rule_t *r) {
	size_t i;
	for(i = 0; i < queue_size(r->commands); i++) {
		if(system(queue_at(r->commands, i)) != 0) {
			//perror("parmake: could not run command\n");
			exit_cleanup(EXIT_FAILURE);
		}
	}
}

boolean_t is_dependent(rule_t *r) {
	queue_t *q = r->deps;
	size_t i; char *target;
	for(i = 0; i < queue_size(q); i++) {
		target = queue_at(q, i);
		if(is_rule(target) && !is_satisfied(target)) {
			return true;
		}
	}
	return false;
}

boolean_t is_rule(char *target) {
	//no mutex needed never going to change
	return search_queue(&task_history, target) != NULL ? true : false;
}

boolean_t is_satisfied(char *target) {
	boolean_t satisfied = false;
	//readers critical section
	pthread_mutex_lock(&m_completed_tasks);
	readers++;
	while(writers) {
		pthread_cond_wait(&reader_cv, &m_completed_tasks);
	}
	reading++;
	pthread_mutex_unlock(&m_completed_tasks);
	//reading
	if(search_queue(&completed_tasks, target) != NULL) {
		satisfied = true;
	}
	//reading
	pthread_mutex_lock(&m_completed_tasks);
	reading--;
	readers--;
	if(writers && reading == 0) {
		pthread_cond_signal(&writer_cv);
	}
	pthread_mutex_unlock(&m_completed_tasks);
	return satisfied;
}

boolean_t is_file(char *target) {
	//try to access the file
	return access(target, F_OK) == 0 ? true : false;
}

boolean_t is_file_dependent(rule_t *r) {
	queue_t *q = r->deps;
	size_t i; char *target;
	for(i = 0; i < queue_size(q); i++) {
		target = queue_at(q, i);
		if(is_rule(target) && !is_file(target)) {
			return true;
		}
	}
	return false;
}