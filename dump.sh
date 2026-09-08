cmake -S . -B build -DTRIOPT_DUMP=ON
cmake --build build
ctest --test-dir build -R '^Dump Triangles$' -V
cmake -S . -B build -DTRIOPT_DUMP=OFF
cmake --build build
