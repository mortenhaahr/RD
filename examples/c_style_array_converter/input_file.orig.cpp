


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

//static std::array<int, 5>
static int normal_array_static[5];
//static std::array<const int, 5>
static const int normal_const_array_static[5] {0, 1, 2, 3, 4};
//static std::array<int*, 2>
static int* pointer_array_static[2];
//static std::array<const int*, 2>
static const int* const_pointer_array_static[2];
//static std::array<int* const, 1>
static int* const non_movable_pointer_array_static[1] = {normal_array_static};
//static std::array<const int* const, 2>
static const int* const non_modifyable_non_movable_pointer_array_static[2] = {normal_array_static, normal_const_array_static};

int main() {

    //std::array<int, 5>
    int normal_array[5];
    //std::array<const int, 5>
    const int normal_const_array[5] {0, 1, 2, 3, 4};
    //std::array<int*, 2>
    int* pointer_array[2];
    //std::array<const int*, 2>
    const int* const_pointer_array[2];
    //std::array<int* const, 2>
    int* const non_movable_pointer_array[1] = {normal_array};
    //std::array<const int* const, 2>
    const int* const non_modifyable_non_movable_pointer_array[2] = {normal_array, normal_const_array};

}