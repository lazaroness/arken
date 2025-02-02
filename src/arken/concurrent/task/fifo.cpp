#include <arken/concurrent/task/fifo.h>
#include <arken/os.h>
#include <cstdio>
#include <lua/lua.hpp>
#include <arken/mvm.h>
#include <arken/json.h>
#include <arken/concurrent/core.h>

namespace arken {
namespace concurrent {
namespace task {

fifo::fifo()
{
  m_uuid      = os::uuid();
  m_microtime = os::microtime();
  fifo::actives()++;

  m_shared.name("arken.concurrent.task.fifo");
}

fifo::~fifo()
{
  fifo::actives()--;
}

std::queue<fifo::node> & fifo::fifo_queue()
{
  static std::queue<fifo::node> s_fifo_queue;
  return s_fifo_queue;
}

void fifo::run()
{
  while (true) {

    fifo::node node = dequeue();

    if( !node ) {
      break;
    }
    this->swap(node.shared());
    node.run();

    std::unique_lock<std::mutex> lck(fifo::mutex());
    running().erase(node.uuid());

  }
}

fifo::node fifo::start(const char * fileName, const char * params, bool release)
{
  std::unique_lock<std::mutex> lck(fifo::mutex());
  fifo::node node = fifo::node(fileName, params, release);
  fifo::push( node );

  if(fifo::actives() < fifo::max()) {
    core::start(new arken::concurrent::task::fifo());
  }

  return node;
}

fifo::node::node()
{}

fifo::node::node(const node &obj)
{
  m_uuid      = obj.m_uuid;
  m_fileName  = obj.m_fileName;
  m_params    = obj.m_params;
  m_microtime = obj.m_microtime;
  m_shared    = obj.m_shared;
  m_finished  = obj.m_finished;
}

fifo::node::node(const char * fileName, const char * params, bool release)
{
  m_fileName  = fileName;
  m_params    = params;
  m_release   = release;
  m_microtime = os::microtime();
  m_finished  = std::shared_ptr<std::atomic<bool>>(new std::atomic<bool>(false));
  m_shared.name("arken.concurrent.task.fifo");
}

void fifo::node::run()
{
  int rv;
  mvm::instance instance = mvm::getInstance(m_release);
  instance.swap(m_shared);

  lua_State * L = instance.state();
  lua_settop(L, 0);

  lua_getglobal(L,  "require");
  lua_pushstring(L, "arken.concurrent.task.fifo");
  rv = lua_pcall(L, 1, 0, 0);
  if (rv) {
    fprintf(stderr, "%s\n", lua_tostring(L, -1));
  }

  if( m_fileName.endsWith(".lua") ) {
    lua_getglobal(L, "dofile");
    lua_pushstring(L, m_fileName);
    rv = lua_pcall(L, 1, 1, 0);
    if (rv) {
      fprintf(stderr, "%s\n", lua_tostring(L, -1));
    }
  } else {
    lua_getglobal(L, "require");
    lua_pushstring(L, m_fileName);
    rv = lua_pcall(L, 1, 1, 0);
    if (rv) {
      fprintf(stderr, "%s\n", lua_tostring(L, -1));
    }
  }

  //lua_pushlstring(L,  m_uuid, 37);
  auto ptr = static_cast<fifo::node **>(lua_newuserdata(L, sizeof(fifo::node*)));
  *ptr = new fifo::node(*this);
  luaL_getmetatable(L, "arken.concurrent.task.fifo.node.metatable");
  lua_setmetatable(L, -2);

  json::decode(L, m_params);

  rv = lua_pcall(L, 2, 0, 0); // alterar para envio de 1
  if (rv) {
    fprintf(stderr, "erro no inicio: %s\n", lua_tostring(L, -1));
  }

  // GC
  if( m_release ) {
    instance.release();
  } else {
    lua_gc(L, LUA_GCCOLLECT, 0);
  }

  (*m_finished.get()) = true;
}

void fifo::push(const fifo::node & node)
{
  fifo::fifo_queue().push( node );
}

fifo::node fifo::dequeue()
{
  std::unique_lock<std::mutex> lck(fifo::mutex());
  if (fifo::fifo_queue().empty()) {
    return {};
  }

  fifo::node n = fifo::fifo_queue().front();
  fifo::fifo_queue().pop();
  running()[n.uuid()] = n.inspect();

  return n;
}

std::unordered_map<string, string> &fifo::running()
{
  static std::unordered_map<string, string> s_running;
  return s_running;
}

string fifo::inspect()
{
  std::unique_lock<std::mutex> lck(fifo::mutex());

  string tmp("{");

  tmp.append("\"running\": [");
  int c = 0;
  for (std::pair<string, string> element : running()) {
    if( c > 0 ) {
      tmp.append(",");
    }
    tmp.append("\"").append(element.second).append("\"");
    c++;
  }
  tmp.append("],");
  tmp.append("\"queue\": ");
  tmp.append(std::to_string(fifo_queue().size()));
  tmp.append("}");
  return tmp;
}

std::mutex & fifo::mutex()
{
  static std::mutex s_mutex;
  return s_mutex;
}

std::atomic<uint32_t> & fifo::actives()
{
  static std::atomic<uint32_t> s_actives{0};
  return s_actives;
}

std::atomic<uint32_t> & fifo::max()
{
  static std::atomic<uint32_t> s_max{core::max().load()};
  return s_max;
}

}  // namespace task
}  // namespace concurrent
}  // namespace arken
