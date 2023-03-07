


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

/*
/// These array types make no sense in a static context, as a static array must have an initializer.... To fix later.
/// If our tool is to be ran on this, then it will technically produce broken code because the user has made an error.
/// If variable type arrays are wanted -> use a vector instead!

//static std::array<int* const, 1>
static int* const non_movable_pointer_array_static[1] = {normal_array_static};
//static std::array<const int* const, 2>
static const int* const non_modifyable_non_movable_pointer_array_static[2] = {normal_array_static, normal_const_array_static};
*/

class Test {

public:
    int ClassArray[5] = {};

private:
    int ClassPrivateArray[4] = {};
    static int StaticClassArray[3];
};

int Test::StaticClassArray[3] = {1, 2, 3};

int main(int argc, const char* argv[]) {

    //std::array<int, 5>
    int normal_array[5];
    //std::array<const int, 5>
    const int normal_const_array[5] {0, 1, 2, 3, 4};
    //std::array<int*, 2>
    int* pointer_array[2];
    //std::array<const int*, 2>
    const int* const_pointer_array[2];

    int int_variable_array[argc];
    //std::array<int* const, 2>
    int* const non_movable_pointer_array[1] = {int_variable_array};
    //std::array<const int* const, 2>
    const int* const non_modifyable_non_movable_pointer_array[2] = {int_variable_array, int_variable_array};


    //Unspecified explicit size
    //std::array<int, 4>
    int unspecified_array[] = {0, 1, 2, 3};

    //std::array<const int, 4>
    const int const_unspecified_array[] = {0, 1, 2, 3};

    //std::array<int*, 4>
    int* int_ptr_unspecified_array[] = {int_variable_array, int_variable_array, int_variable_array, int_variable_array};

    //std::array<const int*, 4>
    const int* const_int_ptrunspecified_array[] = {int_variable_array, int_variable_array, int_variable_array, int_variable_array};

    //std::array<int* const, 2>
    int* const int_ptr_constunspecified_array[] = {int_variable_array, int_variable_array};

    //std::array<const int* const, 2>
    const int* const const_int_ptr_const_unspecified_array[] = {int_variable_array, int_variable_array};

}