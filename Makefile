.PHONY: clean-rescript build-rescript start-rescript
.PHONY: build-example start-example serve-example
.PHONY: clean build start
.PHONY: init-dev fmt webpack-bundle-analyzer

NODE_BINS = node_modules/.bin

EXAMPLE_DIR = example

RESCRIPT_SSG_BIN = ./src/js/bin.mjs

COMMANDS_DIR = $(EXAMPLE_DIR)/src/commands

BUILD_COMMAND_WITHOUT_FILE = ENV_VAR=FOO $(RESCRIPT_SSG_BIN)

clean-rescript:
	$(NODE_BINS)/bsb -clean-world

build-rescript:
	$(NODE_BINS)/bsb -make-world

start-rescript:
	mkdir $(EXAMPLE_DIR)/build; \
	$(NODE_BINS)/bsb -make-world -w

build-example:
	$(BUILD_COMMAND_WITHOUT_FILE) $(COMMANDS_DIR)/Build.bs.js

start-example:
	ENV_VAR=FOO $(RESCRIPT_SSG_BIN) $(COMMANDS_DIR)/Start.bs.js

serve-example:
	$(NODE_BINS)/serve -l 3005 $(EXAMPLE_DIR)/build/public

clean-example:
	rm -rf $(EXAMPLE_DIR)/build
	mkdir $(EXAMPLE_DIR)/build

clean:
	make clean-test
	make clean-rescript
	make clean-example

build: clean
	make build-rescript
	make build-example

build-ci: clean
	make build-rescript
	make test
	make clean-test
	$(BUILD_COMMAND_WITHOUT_FILE) $(COMMANDS_DIR)/BuildWithTerser.bs.js
	make clean-example
	$(BUILD_COMMAND_WITHOUT_FILE) $(COMMANDS_DIR)/BuildWithEsbuildPlugin.bs.js
	make clean-example
	$(BUILD_COMMAND_WITHOUT_FILE) $(COMMANDS_DIR)/BuildWithTerserPluginWithEsbuild.bs.js
	make clean-example
	$(BUILD_COMMAND_WITHOUT_FILE) $(COMMANDS_DIR)/BuildWithTerserPluginWithSwc.bs.js

start: clean build-rescript
	make -j 2 start-rescript start-example

init-dev:
	rm -rf _opam
	opam switch create . 4.06.1 --deps-only

format-reason:
	@$(NODE_BINS)/bsrefmt --in-place -w 80 \
	$(shell find ./src ./example -type f \( -name *.re -o -name *.rei \))

format-rescript:
	@$(NODE_BINS)/rescript format -all

format:
	make format-reason
	make format-rescript

clean-test:
	rm -rf tests/output
	rm -rf coverage

test: clean-test
	$(NODE_BINS)/c8 node ./tests/Tests.bs.js
