


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
static std::array<int, 5> normal_array_static;
//static std::array<const int, 5>
static std::array<const int, 5> normal_const_array_static {0, 1, 2, 3, 4};
//static std::array<int*, 2>
static std::array<int *, 2> pointer_array_static;
//static std::array<const int*, 2>
static std::array<const int *, 2> const_pointer_array_static;

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
    std::array<int, 5> ClassArray = {};

private:
    std::array<int, 4> ClassPrivateArray = {};
    static std::array<int, 3> StaticClassArray;
};

std::array<int, 3> StaticClassArray = {1, 2, 3};

int main(int argc, const char* argv[]) {

    //std::array<int, 5>
    std::array<int, 5> normal_array;
    //std::array<const int, 5>
    std::array<const int, 5> normal_const_array {0, 1, 2, 3, 4};
    //std::array<int*, 2>
    std::array<int *, 2> pointer_array;
    //std::array<const int*, 2>
    std::array<const int *, 2> const_pointer_array;

    int int_variable_array[argc];
    //std::array<int* const, 2>
    std::array<int *const, 1> non_movable_pointer_array = {int_variable_array};
    //std::array<const int* const, 2>
    std::array<const int *const, 2> non_modifyable_non_movable_pointer_array = {int_variable_array, int_variable_array};


    //Unspecified explicit size
    //std::array<int, 4>
    std::array<int, 4> unspecified_array = {0, 1, 2, 3};

    //std::array<const int, 4>
    std::array<const int, 4> const_unspecified_array = {0, 1, 2, 3};

    //std::array<int*, 4>
    std::array<int *, 4> int_ptr_unspecified_array = {int_variable_array, int_variable_array, int_variable_array, int_variable_array};

    //std::array<const int*, 4>
    std::array<const int *, 4> const_int_ptrunspecified_array = {int_variable_array, int_variable_array, int_variable_array, int_variable_array};

    //std::array<int* const, 2>
    std::array<int *const, 2> int_ptr_constunspecified_array = {int_variable_array, int_variable_array};

    //std::array<const int* const, 2>
    std::array<const int *const, 2> const_int_ptr_const_unspecified_array = {int_variable_array, int_variable_array};

}