@echo on
cd native
cmake -B build
cmake --build build --config Release
cd ..
.\gradlew.bat clean build 2>&1
echo 1
pause