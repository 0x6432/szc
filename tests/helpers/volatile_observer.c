#define _GNU_SOURCE
#include <stdbool.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

/* Linux/x86_64 test instrumentation: fault once for each actual memory access. */
static unsigned char *page;
static size_t page_size;
static volatile sig_atomic_t active;
static volatile sig_atomic_t pending;
static volatile sig_atomic_t reads;
static volatile sig_atomic_t writes;
static volatile sig_atomic_t event_count;
static char events[64];

static void
observe_fault(int signal_number, siginfo_t *info, void *context)
{
	ucontext_t *cpu = context;
	if (signal_number == SIGTRAP && pending) {
		if (mprotect(page, page_size, PROT_NONE) != 0)
			_exit(91);
		cpu->uc_mcontext.gregs[REG_EFL] &= ~(greg_t)0x100;
		pending = 0;
		return;
	}
	uintptr_t address = (uintptr_t)info->si_addr;
	if (signal_number != SIGSEGV || !active || address < (uintptr_t)page || address >= (uintptr_t)page + page_size)
		_exit(92);
	if (mprotect(page, page_size, PROT_READ | PROT_WRITE) != 0)
		_exit(93);
	bool write = (cpu->uc_mcontext.gregs[REG_ERR] & 2) != 0;
	if (write)
		writes++;
	else
		reads++;
	if (event_count >= (sig_atomic_t)sizeof(events))
		_exit(94);
	events[event_count++] = write ? 'w' : 'r';
	pending = 1;
	cpu->uc_mcontext.gregs[REG_EFL] |= 0x100;
}

void *observe_memory(size_t offset);
bool observe_finish(uint32_t expected_reads, uint32_t expected_writes);
bool observe_finish_compound(void);
bool observe_finish_copy(void);

void *
observe_memory(size_t offset)
{
	if (page == NULL) {
		long size = sysconf(_SC_PAGESIZE);
		if (size <= 0)
			_exit(95);
		page_size = (size_t)size;
		page = mmap(NULL, page_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (page == MAP_FAILED)
			_exit(96);
		struct sigaction handler = {0};
		handler.sa_sigaction = observe_fault;
		handler.sa_flags = SA_SIGINFO;
		if (sigemptyset(&handler.sa_mask) != 0 || sigaction(SIGSEGV, &handler, NULL) != 0 || sigaction(SIGTRAP, &handler, NULL) != 0)
			_exit(97);
	}
	if (active || offset + 8 >= page_size)
		_exit(98);
	reads = 0;
	writes = 0;
	event_count = 0;
	pending = 0;
	active = 1;
	return page + offset;
}

bool
observe_finish(uint32_t expected_reads, uint32_t expected_writes)
{
	active = 0;
	bool ok = reads == (sig_atomic_t)expected_reads && writes == (sig_atomic_t)expected_writes;
	if (expected_reads == 3 && expected_writes == 3) {
		const char expected[] = "wwrrwr";
		if (event_count != 6)
			return false;
		for (size_t index = 0; index < 6; index++)
			if (events[index] != expected[index])
				ok = false;
	}
	return ok;
}

/* LLVM may use a single read-modify-write addb for the compound assignment.
 * Its page fault is classified as a write; separate load/store helpers fault twice.
 */
bool
observe_finish_compound(void)
{
	active = 0;
	if (writes != 2)
		return false;
	if (reads == 2 && event_count == 4)
		return events[0] == 'w' && events[1] == 'r' && events[2] == 'w' && events[3] == 'r';
	if (reads == 1 && event_count == 3)
		return events[0] == 'w' && events[1] == 'w' && events[2] == 'r';
	return false;
}

/* Scalar memcpy must read each element before storing it, with no extra access. */
bool
observe_finish_copy(void)
{
	active = 0;
	if (reads != 3 || writes != 3 || event_count != 6)
		return false;
	for (size_t index = 0; index < 6; index++)
		if (events[index] != (index % 2 == 0 ? 'r' : 'w'))
			return false;
	return true;
}
