typedef struct {
    char tag;
    char xx;
    short index; // index into allocs_ptr array
} *lisp;

void reportAllocs();
lisp lispinit();
void lisprun(lisp* envp);

lisp princ(lisp x);
lisp terpri();


