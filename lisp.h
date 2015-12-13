typedef struct {
    char tag;
    char xx;
    short index; // index into allocs_ptr array, -1 if not in index
} *lisp;

void report_allocs(int verbose);
lisp lisp_init();
void lisp_run(lisp* envp);

void maybeGC();

lisp apply(lisp f, lisp args);
lisp eval(lisp e, lisp* envp);
lisp funcall(lisp f, lisp args, lisp* envp, lisp e, int noeval);

lisp princ(lisp x);
lisp terpri();


