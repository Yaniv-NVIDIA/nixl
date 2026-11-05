#include <vector>
#include <string>
#include <cstdlib>
#include <cassert>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <utility>
struct xferBenchIOV { uint64_t addr,len; int devId; std::string metaInfo; xferBenchIOV(uint64_t a,uint64_t l,int d,std::string m):addr(a),len(l),devId(d),metaInfo(m){} };
struct xferBenchWorker {};
const std::string XFERBENCH_BACKEND_DOCA_MEMOS="DOCA_MEMOS";
struct xferBenchConfig { static inline std::string backend="DOCA_MEMOS"; static inline size_t max_batch_size=16,pipeline_depth=2; };
std::pair<size_t,size_t> getStrideScheme(xferBenchWorker&,int){return {1,0};}
static std::vector<std::vector<xferBenchIOV>> oldCreateTransferDescLists(xferBenchWorker &worker,
                                                                      std::vector<std::vector<xferBenchIOV>> &iov_lists,
                                                                      size_t block_size,
                                                                      size_t batch_size,
                                                                      int num_threads) {
    auto [count, stride] = getStrideScheme(worker, num_threads);
    std::vector<std::vector<xferBenchIOV>> xfer_lists;

    for (const auto &iov_list: iov_lists) {
        std::vector<xferBenchIOV> xfer_list;

        for (const auto &iov : iov_list) {
            for (size_t i = 0; i < count; i++) {
                size_t dev_offset = ((i * stride) % iov.len);

                for (size_t j = 0; j < batch_size; j++) {
                    size_t block_offset = ((j * block_size) % iov.len);
                    if (block_offset + block_size > iov.len) {
                        // Prevent memory overflow when iov.len is not divisible by block_size
                        block_offset = 0;
                    }
                    xfer_list.push_back(xferBenchIOV((iov.addr + dev_offset) + block_offset,
                                                     block_size,
                                                     iov.devId,
                                                     iov.metaInfo));
                }
            }
        }

        xfer_lists.push_back(xfer_list);
    }

    return xfer_lists;
}
static std::vector<std::vector<xferBenchIOV>> createTransferDescLists(xferBenchWorker &worker,
                                                                      std::vector<std::vector<xferBenchIOV>> &iov_lists,
                                                                      size_t block_size,
                                                                      size_t batch_size,
                                                                      int num_threads) {
    auto [count, stride] = getStrideScheme(worker, num_threads);
    std::vector<std::vector<xferBenchIOV>> xfer_lists;

    for (const auto &iov_list: iov_lists) {
        std::vector<xferBenchIOV> xfer_list;

        for (const auto &iov : iov_list) {
            size_t cycle_len = iov.len;
            const char *reuse = std::getenv("NIXLBENCH_REUSE_LOCAL");
            if (reuse && std::strtoull(reuse, nullptr, 10) != 0 &&
                std::getenv("NIXLBENCH_MEMOS_KEYS_PER_THREAD") &&
                xferBenchConfig::backend == XFERBENCH_BACKEND_DOCA_MEMOS) {
                // Hugepage rounding is allocation padding, not additional reuse slots.
                cycle_len = block_size * xferBenchConfig::max_batch_size *
                            xferBenchConfig::pipeline_depth;
                if (count != 1 || cycle_len == 0 || cycle_len > iov.len) std::abort();
            }
            for (size_t i = 0; i < count; i++) {
                size_t dev_offset = ((i * stride) % cycle_len);

                for (size_t j = 0; j < batch_size; j++) {
                    size_t block_offset = ((j * block_size) % cycle_len);
                    if (block_offset + block_size > cycle_len) {
                        // Prevent memory overflow when iov.len is not divisible by block_size
                        block_offset = 0;
                    }
                    xfer_list.push_back(xferBenchIOV((iov.addr + dev_offset) + block_offset,
                                                     block_size,
                                                     iov.devId,
                                                     iov.metaInfo));
                }
            }
        }

        xfer_lists.push_back(xfer_list);
    }

    return xfer_lists;
}

int main() {
 setenv("NIXLBENCH_REUSE_LOCAL","1",1);setenv("NIXLBENCH_MEMOS_KEYS_PER_THREAD","8192",1);
 xferBenchWorker w;int cases=0,old_failures=0;
 for(size_t kib:{128,256,508,512,1020,1024,2048,4096,8192})for(size_t workers:{8,4}) {
  size_t block=kib*1024,batch=workers==8?16:32,slots=batch*2;
  xferBenchConfig::max_batch_size=batch;
  size_t requested=slots*block*workers,total=(requested+2097151)/2097152*2097152;
  size_t region=(total/workers+2097151)/2097152*2097152;
  std::vector<std::vector<xferBenchIOV>> input;
  for(size_t t=0;t<workers;t++)input.push_back({xferBenchIOV(0x100000000ULL+t*0x100000000ULL,region,0,"meta")});
  auto old=oldCreateTransferDescLists(w,input,block,8192,workers);
  auto got=createTransferDescLists(w,input,block,8192,workers);
  bool old_ok=true;
  for(size_t t=0;t<workers;t++) {
   std::vector<uint64_t> unique;
   for(size_t j=0;j<8192;j++) {
    uint64_t expected=input[t][0].addr+(j%slots)*block;
    assert(got[t][j].addr==expected && got[t][j].len==block && got[t][j].devId==0 && got[t][j].metaInfo=="meta");
    assert(got[t][j].addr+block<=input[t][0].addr+region);
    if(j<slots)unique.push_back(got[t][j].addr);
    old_ok &= old[t][j].addr==expected;
    if(kib!=508 && kib!=1020)assert(old[t][j].addr==got[t][j].addr);
   }
   std::sort(unique.begin(),unique.end());assert(std::unique(unique.begin(),unique.end())==unique.end());
   assert(got[t][slots-1].addr+block<=input[t][0].addr+region);
  }
  if(!old_ok)old_failures++;
  cases++;
 }
 assert(cases==18 && old_failures==4);
 unsetenv("NIXLBENCH_REUSE_LOCAL");
 xferBenchConfig::max_batch_size=16;
 std::vector<std::vector<xferBenchIOV>> input={{xferBenchIOV(0x100000000ULL,16777216,0,"meta")}};
 auto old=oldCreateTransferDescLists(w,input,520192,8192,1);auto got=createTransferDescLists(w,input,520192,8192,1);
 for(size_t j=0;j<8192;j++)assert(old[0][j].addr==got[0][j].addr);
 std::cout<<"ACTUAL_DESCRIPTOR_METHOD_FIXTURE_PASS:18 geometries,4 reproduced old failures,14 unchanged aligned cases;full8192key mappings;distinct lanes;allocation bounds;default path unchanged\n";
}
