#include <fstream>
#include <arken/base>
#include <arken/digest/sha1.h>

extern "C" {
#include <openssl/sha.h>
}

namespace arken {
namespace digest {

char * sha1::hash(const char * data, int length)
{
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const unsigned char *>(data), length, reinterpret_cast<unsigned char *>(&hash));

  auto result = new char[41];

  for(int i = 0; i < 20; i++)
    sprintf(&result[i*2], "%02x", (unsigned int)hash[i]);

  result[40] = '\0';
  return result;
}

char * sha1::file(const char * path)
{
  char * buffer;
  std::ifstream file;
  int length;
  file.open(path);
  file.seekg(0, std::ios::end);
  length = file.tellg();
  file.seekg(0, std::ios::beg);
  buffer = new char[length];
  file.read(buffer, length);
  file.close();
  char * result = hash(buffer, length);
  delete[] buffer;

  return result;
}

} // namespace digest
} // namespace arken
