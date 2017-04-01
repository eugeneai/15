#include "stdio.h"
#include "stdlib.h"
//#include "malloc.h"
#include "time.h"
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>

// This progran does not work now.

#define N 4
#define DEF_SHUFFLE 100
#define MAPPER_SIZE 1000
#define PAGING 10000
#define min(x,y) ((x)>(y) ? (y): (x))
int SHUFFLE = DEF_SHUFFLE;
int MR = MAPPER_SIZE;
int MC = MAPPER_SIZE;
int USE_HEURISTIC = 1;
#define NUMTHREADS 10
// alloc size in cells
#define ALLOC_SIZE 65536
#define map(r,c) mapper[(r)*MC + (c)]

pthread_mutex_t mapper_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t choice_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t free_list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t free_alloc_mutex = PTHREAD_MUTEX_INITIALIZER;

void check_ref(const void * ref) {
    if (!ref) {
        fprintf(stderr, "Cannot allocate memory!\n");
        exit(-1);
    };
}

typedef unsigned char elem;
typedef struct state_s state;
typedef struct state_s {
  state * prev; // Previous state in a possible solution path
  state * next;
  elem field[N][N];
  elem x, y;
  int g, h;
} state_s;

const state final = {
    NULL,NULL,
    {{1,2,3,4}, {5,6,7,8}, {9,10,11,12}, {13,14,15, 0}},
    N-1, N-1,
    0,0
};

// Allocators

typedef struct state_mem_s state_mem;
struct state_mem_s {
    state_mem * prev;
    state * mem;
};

state_mem * state_tail = NULL;
state * state_free_list = NULL;

void state_mem_allocate_unit() {
  state_mem * n = NULL;
     state * p;
     int i;
     // FIXME Start critical section
     n = (state_mem *) malloc(sizeof(state_mem));
     check_ref(n);
     n->mem = (state *) malloc(sizeof(state) * ALLOC_SIZE);
     check_ref(n->mem);
     n->prev = state_tail;
     state_tail = n;
     // make free list;
     pthread_mutex_lock(&free_list_mutex);
     for (i=0;i<ALLOC_SIZE;i++) {
         p=&(n->mem[i]);
         assert(p!=state_free_list);
         p->prev = state_free_list;
         state_free_list = p;
     };
     pthread_mutex_unlock(&free_list_mutex);
     // FIXME Stop critical section
};

int vx[N*N];
int vy[N*N];

int heuristic(state * st) {
    int x,y,h=0, it;
    if (USE_HEURISTIC) {
        for (x=0;x<N;x++) {
            for (y=0;y<N;y++) {
                it=st->field[x][y];
                h += abs(vx[it]-x) + abs(vy[it]-y);
            };
        };
        return h * USE_HEURISTIC;
    } else return 0;
}

void positions_init() {
    int x,y;
    for (x=0;x<N;x++) {
        for (y=0;y<N;y++) {
            vx[final.field[x][y]]=x;
            vy[final.field[x][y]]=y;
        };
    };
};

state * state_new(state * prev, state * next, int g) {
    state * c;
    // FIXME Start critical section
    // Alocate unit from free list
    if (state_free_list != NULL) {
      pthread_mutex_lock(&free_list_mutex);
      c=state_free_list;
      state_free_list=state_free_list->prev;
      pthread_mutex_unlock(&free_list_mutex);
    } else {
      pthread_mutex_lock(&free_alloc_mutex);
      state_mem_allocate_unit();
      pthread_mutex_unlock(&free_alloc_mutex);
      c = state_new(prev, next, g);
    };
    check_ref(c);

    c->prev = prev;
    assert(c!=next);
    c->next = next;
    c->g = g;
    c->h = 0;
    return c;
}

state * state_copy(const state * st) {
    state * c = state_new (NULL, NULL, 0);
    int i,j;
    for (i=0;i<N;i++) {
        for (j=0;j<N;j++) {
            c->field[i][j] = st->field[i][j];
        };
    };
    c->x = st->x;
    c->y = st->y;
    c->next = st->next;
    c->prev = st->prev;
    c->g = st->g;
    c->h = st->h;
    return c;
}

int state_equal(const state * p1, const state * p2) {
    int i,j;
    if (p1->x != p2->x || p1->y != p2->y) return 0;
    for (i=0;i<N;i++) {
        for (j=0;j<N;j++) {
            if (p1->field[i][j] != p2->field[i][j]) return 0;
        };
    };
    return 1;
}

int state_final(const state * st) {
    if (st->x != N-1 || st->y !=N-1) return 0;
    return state_equal(st, &final);
};

void state_free(state * st) {
    st->next=state_free_list;
    state_free_list=st;
}

void state_print(const state * st) {
    int i, j;
    for (i=0;i<N;i++) {
        for (j=0;j<N;j++) {
            printf("%2u ", st->field[i][j]);
        };
        printf("\n");
    };
    printf("x=%i, y=%i\n", st->x, st->y);
    printf("State(%02i,%02i[%02i])\n", st->g, st->h, st->g + st->h);
}

void state_print_list(state * st) {
    state * c = st;
    printf("<---List--->\n");
    while (c!=NULL) {
        state_print(c);
        c=c->next;
    };
    printf("<---End List--->\n");
}

int state_print_solution(state * st) {
    int rc;
    if (st == NULL) {
        printf("<---solution--->\n");
        return 0;
    };
    rc = state_print_solution(st->prev);
    printf("%i Step ------------\n", rc);
    state_print(st);
    return rc + 1;
}

state * state_move(const state * st, int dx, int dy) {
    state * c;
    int tmp;
    int x=st->x, y=st->y;
    int nx=x+dx;
    int ny=y+dy;
    if (nx<0 || ny<0 || nx>=N || ny>=N) {
        return NULL;
    };
    c = state_copy(st);
    tmp=c->field[x][y];
    c->field[x][y]=c->field[nx][ny];
    c->field[nx][ny]=tmp;
    c->x=nx;
    c->y=ny;
    c->h=heuristic(c);
    c->g=st->g + 1;
    return c;
}

int adx[4] = { 0,  0, -1, 1};
int ady[4] = { 1, -1,  0, 0};

state * state_shuffle(const state * st) {
    state * c = state_copy(st), *nc;
    int dx, dy, i, l = 0, ox, oy;
    for (i=0;i<SHUFFLE;i++) {
        dx = adx[l];
        dy = ady[l];
        l = ( (l + rand()) >> 4 ) & 3;
        nc=state_move(c, dx, dy);
        if (nc == NULL) {
            continue;
        };
        ox = c->x;
        oy = c->y;
        //l = ( l + c->field[N- ox %2][N-oy] ) % 4;
        state_free(c);
        c=nc;
    };
    return c;
}

state * state_up(const state * st) {
    return state_move(st, -1, 0);
}

state * state_down(const state * st) {
    return state_move(st, 1, 0);
}

state * state_left(const state * st) {
    return state_move(st, 0, -1);
}

state * state_right(const state * st) {
    return state_move(st, 0, 1);
}

int state_check_presence (state * st, const state * s) {
    state *c = st;
    while (c != NULL) {
        if (state_equal(c, s)) {
            return 1;
        };
        c=c->prev;
    };
    return 0;
};

state ** mapper;

int state_insert (state * st, state * s) {
    // insert after st new state s,
    // if s is wasn't present on the path
    state * c, *p;
    int g, h, gh;
    int i, mi;
    s->next=NULL;
    s->prev=st;
    /*
    printf("\nInsert after\n");
    state_print(st);
    printf("result\n");
    state_print(s);
    */
    if (state_check_presence (st, s)) {
        state_free(s);
        return 0;
    };
    g=s->g; h=s->h;
    gh=g+h;


    c = map(g,h);
    if (c != NULL) {
      pthread_mutex_lock(&mapper_mutex);
      s->next = c->next;
      c->next = s;
      map(g,h) = s;
      pthread_mutex_unlock(&mapper_mutex);
      return 1;
    };
    mi = gh;
    i = g;
    while (mi>0) {
        while (mi-i>h && i<MR) {
           c = map(i,mi-i);
           if (c != NULL) {
             pthread_mutex_lock(&mapper_mutex);
             s->next = c->next;
             c->next = s;
             map(g,h) = s;
             pthread_mutex_unlock(&mapper_mutex);
             return 1;
           };
           i++;
        };
        mi--;
        i = 0;
    };
    /*
    s->next = st->next;
    st->next = s;
    map(g,h) = s;
    return 1;
    */

    c=st->next; // start with next element of the list
    p=st;
    while (c != NULL && c->g+c->h<=gh) {
        p=c;
        c=p->next;
        assert (p->next != p);
    };
    pthread_mutex_lock(&mapper_mutex);
    if (c == NULL) {
        p->next = s;
    } else {
        s->next = c;
        p->next = s;
    };
    assert (p->next != p);
    map(g,h) = s;
    pthread_mutex_unlock(&mapper_mutex);
    return 1;
}

int state_after_all(state * st) {
    int n=0;
    state * nst;
    if ((nst=state_up(st)) != NULL)
        n += state_insert(st, nst);
    if ((nst=state_down(st)) != NULL)
        n += state_insert(st, nst);
    if ((nst=state_left(st)) != NULL)
        n += state_insert(st, nst);
    if ((nst=state_right(st)) != NULL)
        n += state_insert(st, nst);
    return n;
}

void done() {
    state_mem *sh, *sp;
    sh=sp=state_tail;
    while (sh!=NULL) {
        free(sh->mem);
        sp=sh;
        sh=sh->prev;
        free(sp);
    };
    free(mapper);
};

void state_unmap(state * st) {
    int g = st->g, h=st->h;
    state *c = map(g,h);
    if (c == st) {
        map(g,h) = NULL;
    };
};

void mapper_init() {
    int i, j;
    mapper = (state * *) malloc(MC*MR*sizeof(state *));
    for (i=0;i<MR;i++) {
        for (j=0;j<MC;j++) {
            map(i,j)=NULL;
        };
    };
};

void init(int argc, char ** argv) {
    int seed = 1;
    positions_init();

    if (argc>3) USE_HEURISTIC = atoi(argv[3]);
    if (argc>2) seed = atoi(argv[2]);
    if (argc>1) SHUFFLE = atoi(argv[1]);
    if (argc==1) {
      fprintf(stderr, "This is 15 problem solver\n");
      fprintf(stderr, "Usage:%s <shuffle steps> [rand seed] [multiplier]\n", argv[0]);
      fprintf(stderr, "where\n");
      fprintf(stderr, "multiplier is W; f(x)=g(x)+W*h(x)\n");
      exit(-2);
    };

    MR = min(SHUFFLE + 2, 200) ;
    MC = MR * USE_HEURISTIC;
    printf("\nEnvironment:\nUSE_HEURISTIC=%i\nSeed=%i\nSHUFFLE=%i\n",
        USE_HEURISTIC, seed, SHUFFLE);
    mapper_init();

    srand(seed);
};

state * st = NULL;
state * solution = NULL;
int stop_search = 0;
int steps = 0;

state * a_star() {
    int n=1, k=0, d=0;
    state * next;
    int is_final = 0;
    map(st->g,st->h) = st;
    while (1) {
      is_final = state_final(st);
      if (!(stop_search || is_final)) {
        n+=state_after_all(st);
      };
      pthread_mutex_lock(&choice_mutex);
      if (stop_search || is_final) {
        printf("Finishing as sol found.\n");
        if (is_final) {
          solution = st;
          steps = k;
          stop_search = 1;
        }
        assert(solution!=NULL);
        pthread_mutex_unlock(&choice_mutex);
        return solution;
      };
      printf(".");
        //state_print_list(st);
        //state_print_st(s);
        next = st->next;
        if (!next) {
          // stop_search = 1;
          pthread_mutex_unlock(&choice_mutex);
          return NULL;
        };
        if (d==PAGING) {
            d = 0;
            printf("Step %i, set=%i\n", k, n);
            state_print(st);
        };
        st->next = NULL;
        state_unmap(st);
        st=next;
        pthread_mutex_unlock(&choice_mutex);
        n--;
        k++;
        d++;
    };
    return NULL;
}


typedef struct task_s task;
struct task_s{
  // state * start;
  int steps;  // Number pd steps in the solution
  //int nodes;  // Number of open nodes;
  state * solution;
  int finished;
} task_s;

void * thread_work(void * tt) {
  task * t = (task *) tt;
  // assert(t->start);
  t->solution = NULL;
  t->steps = -1; // No solution

  t->finished = 0;
  t->solution = a_star(&(t->steps));
  t->finished = 1;
  return NULL;
}


typedef struct {    /* Used as argument to thread_start() */
  pthread_t thread_id;        /* ID returned by pthread_create() */
  //int       thread_num;       /* Application-defined thread # */
  //char     *argv_string;      /* From command-line argument */
  int rc;
} thread_info;

thread_info I[NUMTHREADS];
task T[NUMTHREADS];

int main (int argc, char ** argv) {
    int steps = 0;
    int step_no = 0;
    int pid = getpid();
    printf("My PID=%i\n", pid);
    //sleep(20);

    state * st_in;
    state * solution;
    int rc_tr, tnum;
    init(argc, argv);

    st_in = state_shuffle(&final);
    st_in->g = 0;
    st_in->prev = NULL;
    st_in->next = NULL;

    st = st_in;

    printf("Puzzle 15 solving program\n");
    printf("Final is:\n");
    state_print(&final);
    printf("Starting is:\n");
    state_print(st);

    solution = NULL;
    stop_search = 0;

    for (tnum=0;tnum<NUMTHREADS;tnum++) {
      // T[tnum].start = st;
        rc_tr = pthread_create(&I[tnum].thread_id, NULL,
                               &thread_work, (void *) &T[tnum]);
        if (rc_tr) {
          fprintf(stderr, "FATAL: Cannot start thread\n!!!");
          exit(-42);
        };
    };

    for (tnum=0;tnum<NUMTHREADS;tnum++) {
      if (T[tnum].finished == 0) {
        printf("WAITING Finishing pthread %i.\n", tnum);
        pthread_join(I[tnum].thread_id, (void *) &I[tnum].rc);
        printf("Pthread %i finished.\n", tnum);
      } else {
        printf("Pthread %i finished ALREADY.\n", tnum);
      }
    }
    // solution = T[0].solution;
    steps = T[0].steps;

    if (solution) {
        printf("After %i variants search a solution found.\n",
            steps);
        step_no=state_print_solution(solution);
        printf("Tested %i states.\n", steps);
    } else {
        printf("No solution.\n");
        return -1;
    };
    done();
    return 0;
    // exit(step_no);
}
