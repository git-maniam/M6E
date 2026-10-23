struct EmptyA{};
struct EmptyB{};
struct NonEmpty{
  int field;
};

#  define MOZ_EMPTY_BASES

struct MOZ_EMPTY_BASES Some : EmptyA, EmptyB {}; // no-error
struct Some0 : EmptyA, EmptyB {}; // expected-error {{Missing MOZ_EMPTY_BASES}}
struct Some1 : EmptyA, NonEmpty {}; // no-error
struct Some2 : EmptyA, EmptyB, NonEmpty {}; // expected-error {{Missing MOZ_EMPTY_BASES}}
