//
// Created by morten on 3/16/23.
//

#include <string_view>
enum Hello { Hi, Dav };

enum class Food { Burger, HotDog, Pizza };

namespace Desserts {
enum class Food { Cheesecake, Cupcake, Pie };

}


constexpr int to_string(Desserts::Food e){
	return 0;
}


namespace N {
namespace S {
enum class Animals { Dog, Cat, Cow };

}
}	// namespace N

constexpr int to_string(N::S::Animals e){
	return 0;
}

int main() {}