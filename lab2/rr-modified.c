#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef uint32_t u32;
typedef int32_t i32;

struct process
{
  u32 pid;
  u32 arrival_time;
  u32 burst_time;

  TAILQ_ENTRY(process) pointers;

  /* Additional fields here */
  u32 remaining_time;  // Remaining time for process
  u32 start_time;      // Time when process starts execution
  bool is_started;   
  /* End of "Additional fields here" */
};

TAILQ_HEAD(process_list, process);

u32 next_int(const char **data, const char *data_end)
{
  u32 current = 0;
  bool started = false;
  while (*data != data_end)
  {
    char c = **data;

    if (c < 0x30 || c > 0x39)
    {
      if (started)
      {
        return current;
      }
    }
    else
    {
      if (!started)
      {
        current = (c - 0x30);
        started = true;
      }
      else
      {
        current *= 10;
        current += (c - 0x30);
      }
    }

    ++(*data);
  }

  printf("Reached end of file while looking for another integer\n");
  exit(EINVAL);
}

u32 next_int_from_c_str(const char *data)
{
  char c;
  u32 i = 0;
  u32 current = 0;
  bool started = false;
  while ((c = data[i++]))
  {
    if (c < 0x30 || c > 0x39)
    {
      exit(EINVAL);
    }
    if (!started)
    {
      current = (c - 0x30);
      started = true;
    }
    else
    {
      current *= 10;
      current += (c - 0x30);
    }
  }
  return current;
}

void init_processes(const char *path,
                    struct process **process_data,
                    u32 *process_size)
{
  int fd = open(path, O_RDONLY);
  if (fd == -1)
  {
    int err = errno;
    perror("open");
    exit(err);
  }

  struct stat st;
  if (fstat(fd, &st) == -1)
  {
    int err = errno;
    perror("stat");
    exit(err);
  }

  u32 size = st.st_size;
  const char *data_start = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (data_start == MAP_FAILED)
  {
    int err = errno;
    perror("mmap");
    exit(err);
  }

  const char *data_end = data_start + size;
  const char *data = data_start;

  *process_size = next_int(&data, data_end);

  *process_data = calloc(sizeof(struct process), *process_size);
  if (*process_data == NULL)
  {
    int err = errno;
    perror("calloc");
    exit(err);
  }

  for (u32 i = 0; i < *process_size; ++i)
  {
    (*process_data)[i].pid = next_int(&data, data_end);
    (*process_data)[i].arrival_time = next_int(&data, data_end);
    (*process_data)[i].burst_time = next_int(&data, data_end);
  }

  munmap((void *)data, size);
  close(fd);
}

int main(int argc, char *argv[])
{
  if (argc != 3)
  {
    return EINVAL;
  }
  struct process *data;
  u32 size;
  init_processes(argv[1], &data, &size);

  u32 quantum_length = next_int_from_c_str(argv[2]);

  struct process_list ready_queue;
  TAILQ_INIT(&ready_queue);

  u32 total_waiting_time = 0;
  u32 total_response_time = 0;

  // Initialize variables
  u32 time = 0;
  bool process_in_queue[size]; // Tracks whether a process is already in the queue
  for (u32 i = 0; i < size; ++i)
  {
    data[i].remaining_time = data[i].burst_time;
    data[i].is_started = false;
    data[i].is_completed = false; // Mark process as incomplete
    process_in_queue[i] = false;
  }

  while (1)
  {
    bool all_done = true;

    // Add newly arrived processes to the ready queue
    for (u32 i = 0; i < size; ++i)
    {
      if (!process_in_queue[i] && !data[i].is_completed && data[i].arrival_time <= time)
      {
        TAILQ_INSERT_TAIL(&ready_queue, &data[i], pointers);
        process_in_queue[i] = true;
      }
    }

    struct process *current = TAILQ_FIRST(&ready_queue);
    if (current == NULL)
    {
      // If the queue is empty but processes are not done, increment time
      for (u32 i = 0; i < size; ++i)
      {
        if (data[i].remaining_time > 0)
        {
          all_done = false;
          time++;
          break;
        }
      }
      if (all_done)
        break;
      continue;
    }

    // Process the current task
    TAILQ_REMOVE(&ready_queue, current, pointers);

    if (!current->is_started)
    {
      current->start_time = time;
      current->is_started = true;
      total_response_time += current->start_time - current->arrival_time;
    }

    if (current->remaining_time > quantum_length)
    {
      time += quantum_length;
      current->remaining_time -= quantum_length;

      // Add newly arrived processes to the queue during this time slice
      for (u32 i = 0; i < size; ++i)
      {
        if (!process_in_queue[i] && !data[i].is_completed && data[i].arrival_time <= time)
        {
          TAILQ_INSERT_TAIL(&ready_queue, &data[i], pointers);
          process_in_queue[i] = true;
        }
      }

      // Requeue the current process
      TAILQ_INSERT_TAIL(&ready_queue, current, pointers);
    }
    else
    {
      // Finish the process
      time += current->remaining_time;
      total_waiting_time += time - current->arrival_time - current->burst_time;
      current->remaining_time = 0;
      current->is_completed = true; // Mark as completed
    }
  }

  // Output the results
  printf("Average waiting time: %.2f\n", (float)total_waiting_time / (float)size);
  printf("Average response time: %.2f\n", (float)total_response_time / (float)size);

  free(data);
  return 0;
}

