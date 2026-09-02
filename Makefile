.PHONY: build clear

build: clear
	@echo "Building..."
	cmake -S . -B build
	ln -sf build/compile_commands.json .
	cmake --build build
	@echo "done"

clear:
	@echo "Clearing the folder..."
	rm -rf build compile_commands.json
	@echo "done"
