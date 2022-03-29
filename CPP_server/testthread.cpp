#include<iostream>
#include<unistd.h>
#include<thread>
using namespace std;

void thr()
{
    while (1)
    {
        sleep(1);
        cout << "thread continue" << endl;
    }
}

int main()
{
    {
        thread t(thr);
        t.detach();
    }
    while (1)
    {
        sleep(1);
        cout << "main continue" << endl;
    }
    return 0;
}