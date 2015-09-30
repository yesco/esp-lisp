typedef struct {
    char tag;
} *lisp;

lisp terpri();
lisp princ(lisp);

void lispinit();
void lisptest();
