UNAME:=$(shell uname)

%:
ifeq ($(UNAME),Linux)
		@ $(MAKE)  $@ -f makefile-nux 
else
		@ $(MAKE) $@ -f makefile-win
endif
