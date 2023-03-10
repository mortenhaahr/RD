#include <array>

//static std::array<int, 5>
static int normal_array_static[5];
//static std::array<const int, 5>
static const int normal_const_array_static[5]{0, 1, 2, 3, 4};
//static std::array<int*, 2>
static int *pointer_array_static[2];
//static std::array<const int*, 2>
static const int *const_pointer_array_static[2];

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

    /*
        This is actually just an int*, so we cannot convert it and keep the semantics even if the syntax looks like it should be able to.
        https://stackoverflow.com/questions/16144535/difference-between-passing-array-fixed-sized-array-and-base-address-of-array-as
        We could potentially go in and reinterpret the line as a varDecl and switch it to a save fixed sized array - but this is just a thought.
        We issue a warning about the fixed sized parameter.
    */
    void testMethodConstSizedParam(int param[5]);

    /*
        This is an int *[4], so this could be converted to a *std::array<int, 4>.
    */
    void testMethodArrayOfArrays(int param[6][4]);

    void testSpacesInParameter(int param [5]);
    void testSpacesInParameter2D(int param [5][4]);
    void testSpacesInParameter2D2(int param [5] [4]);

    /*
        This should not give a warning as it is not a fixed size array.
    */
    void testMethodNonConstParam(int param[]);

private:
    int ClassPrivateArray[4] = {};
    static int StaticClassArray[3];
};

int Test::StaticClassArray[3] = {1, 2, 3};

namespace MyNamespace {
    //std::array<int, 5>
    int namespace_normal_array[5];
    //std::array<const int, 5>
    const int namespace_normal_const_array[5]{0, 1, 2, 3, 4};

    //static std::array<int, 5>
    static int namespace_normal_array_static[5];
    //static std::array<const int, 5>
    static const int namespace_normal_const_array_static[5]{0, 1, 2, 3, 4};

    class ClassInNameSpace {
    public:
        int ClassArray[5] = {};

    private:
        int ClassPrivateArray[4] = {};
        static int StaticClassArray[3];
    };

    int ClassInNameSpace::StaticClassArray[3] = {1, 2, 3};
}

namespace {
    //std::array<int, 5>
    int ann_namespace_normal_array[5];
    //std::array<const int, 5>
    const int ann_namespace_normal_const_array[5]{0, 1, 2, 3, 4};

    //static std::array<int, 5>
    static int ann_namespace_normal_array_static[5];
    //static std::array<const int, 5>
    static const int ann_namespace_normal_const_array_static[5]{0, 1, 2, 3, 4};
}

int main(int argc, const char *argv[]) {

    //std::array<int, 5>
    int normal_array[5];
    //std::array<const int, 5>
    const int normal_const_array[5]{0, 1, 2, 3, 4};
    //std::array<int*, 2>
    int *pointer_array[2];
    //std::array<const int*, 2>
    const int *const_pointer_array[2];

    int int_variable_array[argc];
    //std::array<int* const, 2>
    int *const non_movable_pointer_array[1] = {int_variable_array};
    //std::array<const int* const, 2>
    const int *const non_modifiable_non_movable_pointer_array[2] = {int_variable_array, int_variable_array};


    //Unspecified explicit size
    //std::array<int, 4>
    int unspecified_array[] = {0, 1, 2, 3};

    //std::array<const int, 4>
    const int const_unspecified_array[] = {0, 1, 2, 3};

    //std::array<int*, 4>
    int *int_ptr_unspecified_array[] = {int_variable_array, int_variable_array, int_variable_array, int_variable_array};

    //std::array<const int*, 4>
    const int *const_int_ptr_unspecified_array[] = {int_variable_array, int_variable_array, int_variable_array,
                                                   int_variable_array};

    //std::array<int* const, 2>
    int *const int_ptr_const_unspecified_array[] = {int_variable_array, int_variable_array};

    //std::array<const int* const, 2>
    const int *const const_int_ptr_const_unspecified_array[] = {int_variable_array, int_variable_array};

}