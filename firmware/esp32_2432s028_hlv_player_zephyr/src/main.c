/*
 * Zephyr SMP bring-up for the ESP32-2432S028 HLV player.
 *
 * This intentionally starts with a small microkernel profile: a control/I/O
 * thread pinned to CPU0 and a future decoder thread pinned to CPU1. Threads
 * share the address space, while Zephyr owns preemption, affinity and IPC.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/atomic.h>

#define CONTROL_STACK_SIZE 4096
#define DECODER_STACK_SIZE 3072
#define CONTROL_PRIORITY 4
#define DECODER_PRIORITY 3
#define SD_DRIVE_NAME "SD"
#define SD_MOUNT_POINT "/SD:"

K_THREAD_STACK_DEFINE(control_stack, CONTROL_STACK_SIZE);
K_THREAD_STACK_DEFINE(decoder_stack, DECODER_STACK_SIZE);

static struct k_thread control_thread;
static struct k_thread decoder_thread;
static K_SEM_DEFINE(decoder_tick, 0, 1);
static atomic_t decoder_counter;
static atomic_t decoder_cpu = ATOMIC_INIT(-1);
static FATFS fat_fs;
static struct fs_mount_t sd_mount = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.storage_dev = (void *)SD_DRIVE_NAME,
	.mnt_point = SD_MOUNT_POINT,
};

static void print_play_list(void)
{
	struct fs_file_t file;
	char contents[160];
	ssize_t count;
	int result;

	fs_file_t_init(&file);
	result = fs_open(&file, SD_MOUNT_POINT "/play.txt", FS_O_READ);
	if (result != 0) {
		printk("HLVZ SD play.txt unavailable: %d\n", result);
		return;
	}

	count = fs_read(&file, contents, sizeof(contents) - 1U);
	if (count < 0) {
		printk("HLVZ SD play.txt read failed: %d\n", (int)count);
	} else {
		contents[count] = '\0';
		printk("HLVZ SD play.txt bytes=%d value=%s\n", (int)count,
		       contents);
	}
	(void)fs_close(&file);
}

static int mount_sd(void)
{
	uint32_t block_count = 0U;
	uint32_t block_size = 0U;
	uint64_t capacity_mib;
	int result;

	result = disk_access_ioctl(SD_DRIVE_NAME, DISK_IOCTL_CTRL_INIT, NULL);
	if (result != 0) {
		printk("HLVZ SD init failed: %d\n", result);
		return result;
	}
	result = disk_access_ioctl(SD_DRIVE_NAME, DISK_IOCTL_GET_SECTOR_COUNT,
				   &block_count);
	if (result != 0) {
		printk("HLVZ SD sector-count failed: %d\n", result);
		return result;
	}
	result = disk_access_ioctl(SD_DRIVE_NAME, DISK_IOCTL_GET_SECTOR_SIZE,
				   &block_size);
	if (result != 0) {
		printk("HLVZ SD sector-size failed: %d\n", result);
		return result;
	}

	result = fs_mount(&sd_mount);
	if (result != 0) {
		printk("HLVZ SD mount failed: %d\n", result);
		return result;
	}

	capacity_mib = ((uint64_t)block_count * block_size) >> 20;
	printk("HLVZ SD READY blocks=%u block_size=%u capacity_mib=%u\n",
	       block_count, block_size, (uint32_t)capacity_mib);
	printk("HLVZ SD CRC CMD59=ON READ_CRC16=VERIFY WRITE_CRC16=ON\n");
	print_play_list();
	return 0;
}

static void decoder_entry(void *first, void *second, void *third)
{
	uint32_t state = 0x2432A028U;

	ARG_UNUSED(first);
	ARG_UNUSED(second);
	ARG_UNUSED(third);

	atomic_set(&decoder_cpu, arch_curr_cpu()->id);
	for (;;) {
		/* Placeholder work keeps the CPU1 execution path observable. */
		for (uint32_t index = 0U; index < 4096U; ++index) {
			state = (state << 5) ^ (state >> 2) ^ index;
		}
		atomic_inc(&decoder_counter);
		k_sem_give(&decoder_tick);
		k_sleep(K_MSEC(50));
	}
}

static void control_entry(void *first, void *second, void *third)
{
	uint32_t last_report = 0U;
	int sd_result;

	ARG_UNUSED(first);
	ARG_UNUSED(second);
	ARG_UNUSED(third);

	printk("HLVZ BOOT Zephyr SMP cpus=%d control_cpu=%d decoder_cpu_target=1\n",
	       arch_num_cpus(), arch_curr_cpu()->id);
	sd_result = mount_sd();
	printk("HLVZ SERVICES audio=OFF uart=OFF display=OFF sd=%s\n",
	       sd_result == 0 ? "READY" : "ERROR");

	for (;;) {
		if (k_sem_take(&decoder_tick, K_SECONDS(2)) != 0) {
			printk("HLVZ IPC decoder timeout\n");
			continue;
		}
		const uint32_t current = (uint32_t)atomic_get(&decoder_counter);

		if (current - last_report >= 20U) {
			printk("HLVZ SMP control_cpu=%d decoder_cpu=%d ticks=%u\n",
			       arch_curr_cpu()->id, (int)atomic_get(&decoder_cpu),
			       current);
			last_report = current;
		}
	}
}

int main(void)
{
	k_tid_t control;
	k_tid_t decoder;
	int result;

	control = k_thread_create(&control_thread, control_stack,
				  K_THREAD_STACK_SIZEOF(control_stack), control_entry,
				  NULL, NULL, NULL, CONTROL_PRIORITY, 0, K_FOREVER);
	decoder = k_thread_create(&decoder_thread, decoder_stack,
				  K_THREAD_STACK_SIZEOF(decoder_stack), decoder_entry,
				  NULL, NULL, NULL, DECODER_PRIORITY, 0, K_FOREVER);
	k_thread_name_set(control, "hlv-control-cpu0");
	k_thread_name_set(decoder, "hlv-decoder-cpu1");

	result = k_thread_cpu_pin(control, 0);
	if (result != 0) {
		printk("HLVZ affinity control failed: %d\n", result);
		return result;
	}
	result = k_thread_cpu_pin(decoder, 1);
	if (result != 0) {
		printk("HLVZ affinity decoder failed: %d\n", result);
		return result;
	}

	k_thread_start(control);
	k_thread_start(decoder);
	return 0;
}
