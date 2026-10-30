// Reuse the original native program; only this thread's scheduling priority changes.
#include BM_NATIVE_PRIORITY_HEADER

int main(int argc,char** argv) {
    const auto thread=GetCurrentThread();
    const auto previous=GetThreadPriority(thread);const auto process_class=GetPriorityClass(GetCurrentProcess());
    if(previous==THREAD_PRIORITY_ERROR_RETURN||!process_class)return 2;
    if(!SetThreadPriority(thread,THREAD_PRIORITY_ABOVE_NORMAL))return 2;
    const auto actual=GetThreadPriority(thread);
    const auto initial_class=GetPriorityClass(GetCurrentProcess());
    std::cout<<"NATIVE_PRIORITY_START {\"previous\":"<<previous<<",\"requested\":"<<THREAD_PRIORITY_ABOVE_NORMAL
             <<",\"actual\":"<<actual<<",\"process_class\":"<<initial_class<<"}\n";
    int status=2;
    if(actual==THREAD_PRIORITY_ABOVE_NORMAL&&initial_class==process_class)status=bm_native_normal_entry(argc,argv);
    const auto final_actual=GetThreadPriority(thread);const auto final_class=GetPriorityClass(GetCurrentProcess());
    const bool restored=SetThreadPriority(thread,previous)!=0;
    const auto restored_actual=GetThreadPriority(thread);
    std::cout<<"NATIVE_PRIORITY_END {\"actual\":"<<final_actual<<",\"process_class\":"<<final_class
             <<",\"restored\":"<<(restored?"true":"false")<<",\"restored_actual\":"<<restored_actual<<"}\n";
    if(!restored||restored_actual!=previous||final_actual!=actual||final_class!=process_class)return 2;
    return status;
}
