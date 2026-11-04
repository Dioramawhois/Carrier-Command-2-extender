#include "runtime/mod_loader.h"
#include <iostream>
#define CHECK(x) do { if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';return 1;} }while(0)
int main() {
  using namespace cc2::host;
  CHECK(ScreenSourceSha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  const std::string base="function begin() return 1 end\n";
  CHECK(ScreenSourceSha256("\xef\xbb\xbf" "function begin() return 1 end\r\n")==ScreenSourceSha256(base));
  ScreenSourceProfile profile{ScreenSourceSha256(base),{{base.find('1'),1,"2"}}};
  std::string output="unchanged";
  CHECK(TransformScreenSource({profile},base,&output)==ScreenSourceEditResult::Applied);
  CHECK(output=="function begin() return 2 end\n");
  CHECK(TransformScreenSource({profile},"\xef\xbb\xbf" "function begin() return 1 end\r\n",&output)==ScreenSourceEditResult::Applied);
  output="unchanged";
  CHECK(TransformScreenSource({profile},"function begin() return 7 end\n",&output)==ScreenSourceEditResult::NoProfile);
  CHECK(output=="unchanged");
  auto bad=profile;
  bad.edits[0].offset=base.size()+1;
  CHECK(TransformScreenSource({bad},base,&output)==ScreenSourceEditResult::Rejected);
  CHECK(output=="unchanged");
  bad=profile; bad.edits[0].remove=base.size();
  CHECK(TransformScreenSource({bad},base,&output)==ScreenSourceEditResult::Rejected);
  bad=profile; bad.edits.push_back(bad.edits[0]);
  CHECK(TransformScreenSource({bad},base,&output)==ScreenSourceEditResult::Rejected);
  bad=profile; bad.edits[0].insert="then then";
  CHECK(TransformScreenSource({bad},base,&output)==ScreenSourceEditResult::Rejected);
  CHECK(output=="unchanged");
  bad=profile; bad.edits[0].insert=std::string(256*1024+1,' ');
  CHECK(TransformScreenSource({bad},base,&output)==ScreenSourceEditResult::Rejected);
  bad=profile; bad.edits={{0,0,"error('syntax checking must not execute this')\n"}};
  CHECK(TransformScreenSource({bad},base,&output)==ScreenSourceEditResult::Applied);
  CHECK(TransformScreenSource({profile},std::string(2*1024*1024+1,' '),&output)==ScreenSourceEditResult::Rejected);
  std::cout<<"PASS hash-pinned screen edits: canonical SHA, exact match, bounds, overlap, syntax-only checks and atomic output\n";
}
