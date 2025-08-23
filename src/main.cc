#include <iostream>
#include <ApplicationServices/ApplicationServices.h>

using std::cout;
using std::endl;

void get_all_displays() {
    CGDirectDisplayID ids[10];
    uint32_t display_count;
    if (const CGError err = CGGetActiveDisplayList(10, ids,&display_count)) {
        cout << "Error getting display list: " << err << endl;
    } else {
        /* success */
        cout << "Number of active displays: " << display_count << endl;
       for (uint32_t i = 0; i < display_count; i++) {
           const uint32_t id = ids[i];
           auto [origin, size] = CGDisplayBounds(id);

           cout << "Display ID: " << id << endl;
           cout << origin.x << ", " << origin.y << ", " << size.width << ", " << size.height << endl;
       }
    }
}

int main()
{
    get_all_displays();
    return 0;
}