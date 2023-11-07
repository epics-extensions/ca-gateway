TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

GATEWAY_ROOT ?= $(shell cd $(TOP)/../ && pwd)
TEST_MODULE = gateway_tests
TEST_SCRIPTS = gateway_tests/*.py

TAPFILES = testresults.tap

TEST_FILES += test.db
TEST_FILES += access.txt

# Which pvlist.txt file depends on USE_PCRE
USE_PCRE ?= NO
PVLIST_YES = pvlist_pcre.txt
PVLIST_NO = pvlist_bre.txt

PYTEST_OPTIONS ?= -v
JUNIT_PREFIX ?= gateway_tests_

CLEANS += *.pyc $(TEST_FILES) pvlist.txt gateway.* *.tap

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

ifeq ($(T_A),$(EPICS_HOST_ARCH))

# Environment required by some test programs
export BASE ?= $(EPICS_VERSION).$(EPICS_REVISION)
export EPICS_BASE
export GATEWAY_ROOT

ifeq ($(BASE_3_14),YES)
clean::
	@$(RM) $(CLEANS)
endif

runtests:
	@echo
	@echo "Python is: $(shell which python)"
	@echo "GATEWAY_ROOT=$(GATEWAY_ROOT)"
	@echo "Running pytest with options: '$(PYTEST_OPTIONS)'"
	python -m pytest --junit-xml="result.xml" --junit-prefix="$(JUNIT_PREFIX)" $(TEST_MODULE) $(PYTEST_OPTIONS)

$(TAPFILES): .runtests.tap ;
.INTERMEDIATE: .runtests.tap
.runtests.tap:
	$(MAKE) PYTEST_OPTIONS="$(PYTEST_OPTIONS) --tap-files --tap-combined --tap-outdir=." runtests

# Workaround for 3.14 make rules: special target to avoid circular dependencies
build runtests tapfiles: testfiles testmodule pvlist.txt
testfiles: $(TEST_FILES:%=../%)
	$(ECHO) "Copying test files $(TEST_FILES)"
	@$(CP) $^ .

$(TEST_SCRIPTS): ;

testmodule: $(TEST_SCRIPTS)
	$(ECHO) "Copying test module $(TEST_MODULE)"
	@$(CP) -r ../$(TEST_MODULE) ./

pvlist.txt: ../$(PVLIST_$(USE_PCRE))
	$(ECHO) "Copying $< to $@ (USE_PCRE: $(USE_PCRE))"
	@$(CP) $< $@

.PHONY: build runtests testmodule testfiles clean

endif
