.PHONY: build clear

build: clear
	@echo "Building..."
	cmake -S . -B build
	cmake --build build
	@echo "done"

clear:
	@echo "Clearing the folder..."
	rm -rf build
	@echo "done"
