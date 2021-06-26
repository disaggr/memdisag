#include <plasma/client.h>

#include <arrow/util/logging.h>

#include <unistd.h>
#include <bitset>

using namespace plasma;

ObjectID* object_ids;

void CreateObjects(PlasmaClient& client, size_t n, size_t size) {
  uint8_t* rand_data = new uint8_t[n*size];
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < size; j++) {
      rand_data[i*size+j] = std::rand();
    }
  }
  std::shared_ptr<Buffer>* data = new std::shared_ptr<Buffer>[n];
  std::string metadata = "";
  for (int i = 0; i < n; i++) {
    ARROW_CHECK_OK(client.Create(object_ids[i], size, 
          (uint8_t*) metadata.data(), metadata.size(), &data[i], 0, true));
  }
  for (int i = 0; i < n; i++) {
    // Write some data into the object.
    memcpy(data[i]->mutable_data(), rand_data + i*size, size);
  }
  for (int i = 0; i < n; i++) {
    // Seal the object.
    ARROW_CHECK_OK(client.Seal(object_ids[i]));
  }
  for (int i = 0; i < n; i++) {
    ARROW_CHECK_OK(client.Release(object_ids[i]));
  }
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
  int64_t evicted;
  ARROW_CHECK_OK(client.Evict(1000000000, evicted));

  object_ids = new ObjectID[n];
  for (int i = 0; i < n; i++) {
    std::string id = std::bitset<20>(i).to_string();
    object_ids[i] = ObjectID::from_binary(id);
  }

  CreateObjects(client, n, size);
  
  ARROW_CHECK_OK(client.Disconnect());
}