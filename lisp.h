typedef struct {
    char tag;
} *lisp;

void reportAllocs();
void lispinit();
void lisptest();

lisp princ(lisp x);
lisp terpri();


