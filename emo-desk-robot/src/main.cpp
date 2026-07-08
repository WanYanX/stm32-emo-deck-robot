#include "app.hpp"

int main()
{
    //SystemInit();
    static App& app = App::get_instance();
    app.init();
    app.run();
}