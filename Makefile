.PHONY: build clear

build:
	@echo "Building..."
	cmake -S . -B build
	cmake --build build
	@echo "done"

test: build
	@echo "Running tests..."
	rm -rf /tmp/oopsie
	mkdir -p /tmp/oopsie
	cd build && ctest --output-on-failure
	@echo "Tests done"

clear:
	@echo "Clearing the folder..."
	rm -rf build
	@echo "done"
