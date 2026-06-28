SRCS += mem/mem.c

# Add test target
$(eval $(call test_target,test_mem,test/unity.c mem/mem_test.c mem/mem.c))
