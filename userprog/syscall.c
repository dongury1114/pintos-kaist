#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "userprog/process.h"
#include "threads/synch.h"
#include <console.h>
#include "filesys/file.h"
#include "include/threads/palloc.h"
// #include "device/input.h"

// 추가 : 시스템콜 전역변수 락
typedef int pid_t;

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void check_address(void *addr);
void halt(void);
void exit(int status);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int read(int fd, void *buffer, unsigned size);
pid_t fork(const char *thread_name);
int wait(pid_t pid);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
int exec(const char *cmd_line);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081					/* Segment selector msr */
#define MSR_LSTAR 0xc0000082				/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
													((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
						FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&sys_lock);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	/*
 인자 들어오는 순서:
 1번째 인자: %rdi
 2번째 인자: %rsi
 3번째 인자: %rdx
 4번째 인자: %r10
 5번째 인자: %r8
 6번째 인자: %r9
 */
	// TODO: Your implementation goes here.
	check_address(f->rsp);
	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_FORK:
		/* code */
		memcpy(&thread_current()->parent_if, f, sizeof(struct intr_frame));
		f->R.rax = fork(f->R.rdi);
		break;
	case SYS_EXEC:
		/* code */
		// f->R.rax = exec(f->R.rdi);
		exec(f->R.rdi);
		break;
	case SYS_WAIT:
		/* code */
		f->R.rax = wait(f->R.rdi);
		break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN:
		/* code */
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ:
		/* code */
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
		/* code */
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		/* code */
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		/* code */
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE:
		/* code */
		close(f->R.rdi);
		break;

	default:
		// exit(-1);
		// thread_exit();
		break;
	}
	// printf("system call!\n");
}
// 추가 : 시스템콜!
/* 주소 값이 유저 영역에서 사용하는 주소 값인지 확인 하는 함수
유저 영역을 벗어난 영역일 경우 프로세스 종료(exit(-1)) */
void check_address(void *addr)
{
	if (addr == NULL || is_kernel_vaddr(addr) || pml4_get_page(thread_current()->pml4, addr) == NULL)
	{
		exit(-1);
	}
}
/* 유저 스택에 있는 인자들을 커널에 저장하는 함수
스택 포인터(esp)에 count(인자의 개수) 만큼의 데이터를 arg에 저장 */
// void get_argument(void *rsp, int *arg, int count)
// {
// }

void halt(void)
{
	power_off();
}

void exit(int status)
{
	// struct thread *curr = thread_current();
	// curr->exit_status = status;
	thread_current()->exit_status = status;
	printf("%s: exit(%d)\n", thread_current()->name, status);
	thread_exit();
}

bool create(const char *file, unsigned initial_size)
{
	check_address(file);
	return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
	check_address(file);
	return filesys_remove(file);
}

int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);
	if (fd >= thread_current()->next_fd)
		return -1;
	if (fd < 0 || fd == 1)
		return -1;
	if (fd == 0) //표준 입력
	{
		lock_acquire(&sys_lock);
		int byte = input_getc();
		lock_release(&sys_lock);
		return byte;
	}

	lock_acquire(&sys_lock);
	struct file *read_file = process_get_file(fd);
	if (read_file == NULL)
	{
		lock_release(&sys_lock);
		return -1;
	}
	int _size = file_read(read_file, buffer, size);
	lock_release(&sys_lock);
	return _size;
}

int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);

	if (fd >= thread_current()->next_fd)
	{
		return -1;
	}
	else if (fd < 1)
	{
		return -1;
	}
	else if (fd == 1)
	{
		lock_acquire(&sys_lock);
		putbuf(buffer, size);
		lock_release(&sys_lock);
		return size;
	}
	else
	{
		struct file *write_file = process_get_file(fd);

		if (write_file)
		{
			lock_acquire(&sys_lock);
			int write_byte = file_write(write_file, buffer, size);
			lock_release(&sys_lock);
			return write_byte;
		}
	}
}
// file을 열고 성공하면 fd를 반환 하고 실패하면 -1을 반환
int open(const char *file)
{

	check_address(file);
	struct thread *cur = thread_current();
	struct file *fd = filesys_open(file);
	if (fd)
	{
		for (int i = 2; i < 128; i++)
		{
			if (!cur->fdt[i])
			{
				cur->fdt[i] = fd;
				cur->next_fd = i + 1;
				return i;
			}
		}
		file_close(fd);
	}
	return -1;
}

int filesize(int fd)
{
	struct file *getfile = process_get_file(fd);
	if (getfile == NULL)
		return -1;
	return file_length(getfile);
}

void close(int fd)
{
	if (fd < 2)
	{
		return;
	}
	if (fd >= thread_current()->next_fd)
		return;
	lock_acquire(&sys_lock);
	process_close_file(fd);
	lock_release(&sys_lock);
}

void seek(int fd, unsigned position)
{
	struct file *read_file = process_get_file(fd);
	file_seek(read_file, position);
}

unsigned tell(int fd)
{
	struct file *read_file = process_get_file(fd);
	/*왜 주소가 오지(?)*/
	off_t off = file_tell(read_file);
	if (file_length(read_file) < off || 0 > off)
	{
		return -1;
	}
	else
		return off;
}
/////////////////
pid_t fork(const char *thread_name)
{
	check_address(thread_name);
	return process_fork(thread_name, &thread_current()->parent_if);
}

int exec(const char *cmd_line)
{
	check_address(cmd_line);
	int file_size = strlen(cmd_line) + 1;
	char *fn_copy = palloc_get_page(PAL_ZERO);

	if (fn_copy == NULL)
	{
		return -1;
	}
	strlcpy(fn_copy, cmd_line, file_size); // file 이름만 복사
	int temp = process_exec(fn_copy);
	if (temp == -1)
	{
		exit(-1);
		return -1;
	}
}

int wait(pid_t pid)
{
	return process_wait(pid);
}