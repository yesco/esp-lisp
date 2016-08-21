;; Each line is one expression
;; indented lines are appended to previous expression before parsed
;; only one expression is parsed per (top-level) line
(princ "--- LOADING init.lsp FOR STARTUP ---")
(terpri)

;; init.lsp is loaded at startup, add stuff here
;; TODO: at the moment this is very important to load for the *TR?
(load "env.lsp")

;; enable to use "long words"
;(load "scheme.lsp")
(princ "--- SUCCESSFULLY LOADED init.lsp ---")
(terpri)

