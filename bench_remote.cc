#include <plasma/client.h>

#include <arrow/util/logging.h>

#include <unistd.h>
#include <bitset>
#include <chrono>

using namespace plasma;

using namespace std::chrono;

ObjectID* object_ids;

void GetObjects(PlasmaClient& client, size_t n, size_t size) {
  std::shared_ptr<Buffer> data;
  ObjectBuffer* object_buffers = new ObjectBuffer[n];
  std::shared_ptr<Buffer> buffer;
  auto result = new uint8_t[n*size];
  const uint8_t* rddata;
  int64_t buff_size;
  auto t1 = steady_clock::now();
  ARROW_CHECK_OK(client.Get(object_ids, n, 0, object_buffers));
  auto t2 = steady_clock::now();
  for (int i = 0; i < n; i++) {
    // Retrieve object data.
    buffer = object_buffers[i].data;
    rddata = buffer->data();
    buff_size = buffer->size();
    // Check that the data agrees with what was written in the other process.
    memcpy(result + i*size, rddata, buff_size);
  }
  auto t3 = steady_clock::now();
  printf("%ld, %ld us\n", 
          duration_cast<microseconds>(t2 - t1), 
          duration_cast<microseconds>(t3 - t2));
}

int main(int argc, char** argv) {
  if (argc != 5) { return 1; }
  std::string plasma_socket = argv[1];
  std::string remote_memory_file = argv[2];
  size_t n = strtol(argv[3], nullptr, 0);
  size_t size = strtol(argv[4], nullptr, 0);

  PlasmaClient client;
  ARROW_CHECK_OK(client.MmapRemoteMemory(remote_memory_file));
  ARROW_CHECK_OK(client.Connect(plasma_socket));

  object_ids = new ObjectID[n];
  for (int i = 0; i < n; i++) {
    std::string id = std::bitset<20>(i).to_string();
    object_ids[i] = ObjectID::from_binary(id);
  }

  GetObjects(client, n, size);
  
  ARROW_CHECK_OK(client.Disconnect());
}