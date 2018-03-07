#include <stdio.h>
#include <sys/types.h>
#include <zconf.h>
#include <stdlib.h>
#include <errno.h>
#include <memory.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>

#define SHM_NAME "/memory"

struct mem_type {
	int* shared_memory;
	int n;
};

void syserr(const char *fmt, ...)
{
	va_list fmt_args;

	fprintf(stderr, "ERROR: ");

	va_start(fmt_args, fmt);
	vfprintf(stderr, fmt, fmt_args);
	va_end (fmt_args);
	fprintf(stderr," (%d; %s)\n", errno, strerror(errno));
	exit(1);
}

bool debug = false;
bool debug2 = false;

enum proc_type {A, B};

void procesA(int id, int* shared_memory, int n) {

	if(debug2) printf("pA[%d] pracuje\n", id);
	int* shnum = shared_memory;
	sem_t* semA = (sem_t*)(&shared_memory[2*n]);
	sem_t* semB = &semA[n];
	sem_t* waitA = &semB[n];
	sem_t* waitB = &waitA[1];
	int* changed_A = (int*)(&waitB[1]);
//	int* changed_previous_A = &changed_A[1];
	int* changed_B = &changed_A[2];
//	int* changed_previous_B = &changed_A[3];
	int* finished_A = &changed_A[4];
//	int* finished_B = &changed_A[5];
	sem_t* security = (sem_t*)(&changed_A[6]);

	if(debug) printf("procesA[%d] próbuje przejsc przez semA\n", id);
	sem_wait(&semA[id]);
	if(debug) printf("procesA[%d] przeszedł przez semA\n", id);

	bool begin = true;
	while(begin || *changed_B != 0) {
		if(debug2) printf("procesA[%d] znowu czeka bo *changed_B = %d\n", id, *changed_B);
		begin = false;
		sem_wait(security);
		// porównuje i w razie koniecznośi zamienia liczby t[2*i] oraz t[2*i + 1]
		if(shnum[2*id] > shnum[2*id + 1]) {
			int tmp = shnum[2 * id];
			shnum[2 * id] = shnum[2 * id + 1];
			shnum[2 * id + 1] = tmp;
			(*changed_A)++;
		}
		(*finished_A)++;
		if(*finished_A != n) {
			sem_post(security);
			sem_wait(waitA);
		}
		else {
			(*finished_A) = 0;
			(*changed_B) = 0;
			if(debug) {
				printf("procesA : Tablica wyglada tak : ");
				for(int x = 0; x < 2*n; x++) printf("%d ", shnum[x]);
				printf("\n");
			}
			if(debug2) printf("Ostatni A ---------------------------------------------\n");

			sem_post(security);
			// podnosimy go n-1 razy
			for(int x = 0; x < n-1; x++) sem_post(waitA);
		}

		if(debug) printf("procesA[%d] konczy obrut i budzi\n", id);
		if(id != 0) {
			sem_post(&semB[id-1]);
		}
		if(id != n - 1) {
			sem_post(&semB[id]);
		}
		sem_wait(&semA[id]);
		if(id != 0 && id != n - 1) {
			sem_wait(&semA[id]);
		}
		if(debug2) printf("procesA[%d] skonczył czekać\n", id);
	}
	if(debug2) printf("procesA[%d], pid = %d, KONIEC\n", id, getpid());
}

void procesB(int id, int* shared_memory, int n) {

	if(debug2) printf("pB[%d] pracuje\n", id);
	int* shnum = shared_memory;
	sem_t* semA = (sem_t*)(&shared_memory[2*n]);
	sem_t* semB = &semA[n];
	sem_t* waitA = &semB[n];
	sem_t* waitB = &waitA[1];
	int* changed_A = (int*)(&waitB[1]);
//	int* changed_previous_A = &changed_A[1];
	int* changed_B = &changed_A[2];
	int* changed_previous_B = &changed_A[3];
//	int* finished_A = &changed_A[4];
	int* finished_B = &changed_A[5];
	sem_t* security = (sem_t*)(&changed_A[6]);

	sem_wait(&semB[id]);
	sem_wait(&semB[id]);

	bool begin = true;
	bool end = false;
	while((begin || *changed_A != 0) && end == false) {
		sem_wait(security);
		begin = false;
		if(shnum[2*id + 1] > shnum[2*id + 2]) {
			int tmp = shnum[2*id + 1];
			shnum[2*id + 1] = shnum[2*id + 2];
			shnum[2*id + 2] = tmp;
			(*changed_B)++;
		}
		(*finished_B)++;
		if((*finished_B) != n-1) {
			sem_post(security);
			sem_wait(waitB);
		}
		else {
			(*finished_B) = 0;
			(*changed_A) = 0;
			(*changed_previous_B) = (*changed_B);
			if(debug) {
				printf("procesB : Tablica wyglada tak : ");
				for(int x = 0; x < 2*n; x++) printf("%d ", shnum[x]);
				printf("\n");
			}
			if(debug2) printf("Ostatni B -----------ileZm = %d----------------------------\n", *changed_B);
			sem_post(security);
			for(int x = 0; x < n-2; x++) sem_post(waitB);
		}

		sem_wait(security);
		if((*changed_previous_B) != 0) {
			if(debug) printf("procesB[%d] konczy obrut i budzi\n", id);
			sem_post(security);
			sem_post(&semA[id]);
			sem_post(&semA[id+1]);
			sem_wait(&semB[id]);
			sem_wait(&semB[id]);
			if(debug) printf("procesB[%d] skonczył czekać\n", id);
		}
		else {
			if(debug) printf("procesB[%d] konczy obrut\n", id);
			sem_post(security);
			sem_post(&semA[id]);
			sem_post(&semA[id+1]);
			end = true;
		}
	}
	sem_post(&semA[id]);
	sem_post(&semA[id+1]);
	if(debug2) printf("procesB[%d], pid = %d, KONIEC\n", id, getpid());
}

struct mem_type get_input() {
	struct mem_type input;

	//WCZYTYWANIE DANYCH
	int n;
	if(debug) printf("Podaj N >= 2 : \n");
	scanf("%d", &n);
	if(debug) printf("Podałeś N = %d\n", n);
	int fd_mem = shm_open(SHM_NAME, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if(fd_mem == -1) syserr("shm_open");
	// Zwiększam wielkość SHM, do rozmiarów, jakie będą potrzebne do przechowywania wszystkich potrzebnych danych
	if (ftruncate(fd_mem, 2*n*sizeof(int) + 2*(n+1)*sizeof(sem_t) + 6*sizeof(int) + sizeof(sem_t)) == -1) syserr("truncate");

	if(debug) printf("Podaj mi teraz %d liczb :\n", 2*n);
	int numbers[2*n];
	for(int i = 0; i < 2*n; i++) {
		scanf("%d", &numbers[i]);
	}
	if(debug) printf("Podałeś : ");
	for(int i = 0; i < 2*n; i++) {
		if(debug) printf("%d, ", numbers[i]);
	}
	if(debug) printf("\n");

	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_SHARED;
	// pamięć do przechowywania wszystkich danych współdzielonych
	int* shared_memory = mmap(NULL, 2*n*sizeof(int) + 2*(n+1)*sizeof(sem_t) + 6*sizeof(int) + sizeof(sem_t), prot, flags, fd_mem, 0);

	close(fd_mem);
	shm_unlink(SHM_NAME);

	if(shared_memory == MAP_FAILED){
		if(debug) printf("Jest map_failed\n");
		syserr("mmap");
	}
	else if(debug) printf("Pamięć do przechowywania danych została pomyślnie utworzona\n");

	memcpy(shared_memory, numbers, 2*n*sizeof(int));

	input.shared_memory = shared_memory;
	input.n = n;
	return input;
}

void init_variables(int *shared_memory, int n) {
	// semafor dla procesów z grupy B, do powiadamiania procesów z grupy A o zakończonym kroku sortowania
	sem_t* semA = (sem_t*)(&shared_memory[2*n]);
	// semafor dla procesów z grupy A, do powiadamiania procesów z grupy B o zakończonym kroku sortowania
	sem_t* semB = &semA[n];

	// semafor dla procesów z grupy A, do czekania aż wszystkie procesy z tej grupy zakończą obliczenia
	sem_t* waitA = &semB[n];
	// semafor dla procesów z grupy B, do czekania aż wszystkie procesy z tej grupy zakończą obliczenia
	sem_t* waitB = &waitA[1];

	// changed_A, changed_B - zmienne, potrzebne do weryfikacji,
	// czy dana grupa procesów przestawiła coś w swoim ostatnim kroku sortowania
	int* changed_A = (int*)(&waitB[1]);
	int* changed_previous_A = &changed_A[1];
	int* changed_B = &changed_A[2];
	int* changed_previous_B = &changed_A[3];

	// finished_A, finished_B - ile osób z danej grupy skończyło obliczenia
	// zmienne dzięki którym osoby z grupy, które skończą obliczenia nie jako ostatnie w swojej grupie,
	// będą mogły powiesić się na semaforze swojej grupy (semA, semB)
	int* finished_A = &changed_A[4];
	int* finished_B = &changed_A[5];

	*changed_A = *changed_B = *finished_A = *finished_B = 0;
	*changed_previous_A = *changed_previous_B = 0;
	if(debug) printf("Zmienne 'ile_zmienilo', 'ile_skonczylo' dla grup A, B zostały ustawione na %d\n", *changed_A);

	// semafor ochrona do ochrony powyższych czterech zmiennych
	// (ile_zmieniło_A, changed_B, finished_A, finished_B)
	sem_t* security = (sem_t*)(&changed_A[6]);


	// Ustawiam początkowe wartości semaforów
	if(sem_init(waitA, 1, 0) || sem_init(waitB, 1, 0) || sem_init(security, 1, 1)) {
		syserr("sem_init");
	}
	if(debug) printf("Zainicjalizowałem semafory nienazwane : 'waitA', 'waitB', 'security'\n");
	//semafory dla A
	if(debug) printf("Inicjalizuję semA[0, ..., n-1] ... \n");
	for(int i = 0; i < n; i++) {
		if(sem_init(&semA[i], 1, 1)) {
			syserr("sem_init");
		}
	}
	if(debug) printf("Zainicjalizowałem semafory nienazwane : 'semA[0, ..., n-1]'\n");
	//semafory dla B
	for(int i = 0; i < n; i++) {
		if(sem_init(&semB[i], 1, 0)) {
			syserr("sem_init");
		}
	}
	if(debug) printf("Zainicjalizowałem semafory nienazwane : 'semB[0, ..., n-1]'\n");
	if(debug) printf("*** semB[n-1] nie jest potrzebny, ale dodaję go, po to aby mieć równe tablice\n");
}

void unmap_memory(int *shared_memory, int n) {
	munmap(shared_memory, 2*n*sizeof(int) + 2*(n+1)*sizeof(sem_t) + 6*sizeof(int) + sizeof(sem_t));
}

void run_processes(int* shared_memory, int n, enum proc_type type) {
	sigset_t block_mask;
	sigemptyset(&block_mask);
	sigaddset(&block_mask, SIGUSR1);

	pid_t pid;
	int processes_number = 0;
	switch(type) {
		case A: processes_number = n;		break;
		case B: processes_number = n - 1; 	break;
	}
	for(int i = 0; i <= processes_number - 1; i++) {
		switch(pid = fork()) {
			case -1:
				if (sigprocmask(SIG_BLOCK, &block_mask, 0) == -1) {
					syserr("sigprocmask block");
				}
				killpg(getpgid(getpid()), SIGUSR1);
				unmap_memory(shared_memory, n);
				if (sigprocmask(SIG_UNBLOCK, &block_mask, 0) == -1) {
					syserr("sigprocmask unblock");
				}
				// proces macierzysty umiera bo dostał SIGUSR1
			case 0:
				// jestem dzieckiem
				switch(type) {
					case A:
						procesA(i, shared_memory, n);
						if(debug) printf("procesA[%d] skonczyl\n", i);
						break;
					case B:
						procesB(i, shared_memory, n);
						if(debug) printf("procesB[%d] skonczyl\n", i);
						break;
				}
				exit(0);
		}
	}
}

void wait_for_processes_A_and_B(int n) {
	for(int i = 0; i <= n + n - 2; i++) {
		if(wait(0) == -1) {
			syserr("Error in wait\n");
		}
		if(debug) printf("---------- %d skończyło\n", i);
		if(debug2) printf("---------- %d skończyło\n", i);
	}
}

void print_sorted_numbers(int *shared_memory, int n) {
	if(debug2) printf("Wypisuje posortowane liczby\n");
	for(int i = 0; i < 2 * n; i++) {
		printf("%d\n", shared_memory[i]);
	}
}

void run_Sort_process() {

	struct mem_type input = get_input();
	int* shared_memory = input.shared_memory;
	int n = input.n;
	if(debug2) printf("Wczytałem dane\n");

	init_variables(shared_memory, n);
	if(debug2) printf("Zainicjalizowałem zmienne\n");

	enum proc_type typeA = A, typeB = B;
	run_processes(shared_memory, n, typeA);
	run_processes(shared_memory, n, typeB);

	if(debug2) printf("Uruchomiłem procesy A i B\n");

	wait_for_processes_A_and_B(n);

	if(debug2) printf("Procesy zakończyły działanie\n");

	print_sorted_numbers(shared_memory, n);

	unmap_memory(shared_memory, n);
}

int main() {
	run_Sort_process();
	return 0;
}
