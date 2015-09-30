typedef struct {
    char tag;
    char xx;
    short index;
} *lisp;

void reportAllocs();
lisp lispinit();
void lisptest();

lisp princ(lisp x);
lisp terpri();


