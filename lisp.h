typedef struct {
    char tag;
    char xx;
    short index; // index into allocs_ptr array
} *lisp;

void report_allocs(int verbose);
lisp lisp_init();
void lisp_run(lisp* envp);

lisp princ(lisp x);
lisp terpri();


