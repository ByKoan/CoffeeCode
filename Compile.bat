rmdir /s /q build
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_QUIET=OFF
cmake --build build --config Release -j --verbose
pause
