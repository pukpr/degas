gcc degas.c -shared -fPIC -DLINUX=1 -o Linux/libdegas.so
gnatmake simple_ada_test

