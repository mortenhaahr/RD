


#include <array>

/*
class Test {
public:
    std::array<int, 4> STDArrayInClass;
    int CStyleArrayInClass[4];
};
*/

/*
static std::array<int, 4> STDArrayGlobal;
*/
static int CStyleArrayGlobal[4];

int main() {

//    auto test = CStyleArrayGlobal;
//    Test test;

    std::array<int, 4> STDArrayInMethod;

    const double* CStyleArrayInMethod[5];

    int name[5][7];

 //   test.STDArrayInClass[0] = 0xF4;
 //   test.CStyleArrayInClass[0] = 0xF4;

 //   STDArrayInMethod[0] = 0xF4;
//    CStyleArrayInMethod[0] = 0xF4;

    // Matchers:
    //  	arrayType           initListExpr
    //int ImplicitCStyleArray[] = {0, 4, 5, 6};

}