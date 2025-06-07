# Build configuration
BUILD_DIR = build
CMAKE = cmake
CMAKE_BUILD_TYPE ?= Debug

.PHONY: all build clean

all: build

build:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) ..
	@cd $(BUILD_DIR) && $(CMAKE) --build .

clean:
	@rm -rf $(BUILD_DIR)
	@echo "Build directory cleaned"

# Help target
help:
	@echo "Available targets:"
	@echo "  build  - Build the project (default)"
	@echo "  clean  - Remove build directory and artifacts"
	@echo "  help   - Show this help message" 