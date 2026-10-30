#include "hot_adapter.h"
#include <iostream>
int main() { auto index=make_hot_index({}); index->bulk_load({{"a",1},{"b",2}}); auto value=index->find("b"); if(value!=2)return 1; std::cout<<"HOT native read smoke passed\n"; }
