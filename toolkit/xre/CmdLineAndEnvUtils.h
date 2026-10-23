/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef mozilla_CmdLineAndEnvUtils_h
#define mozilla_CmdLineAndEnvUtils_h

// NB: This code may be used outside of xul and thus must not depend on XPCOM

#if defined(MOZILLA_INTERNAL_API)
#  include "prenv.h"
#  include "prprf.h"
#  include <string.h>
#endif


#include "mozilla/Maybe.h"
#include "mozilla/MemoryChecking.h"
#include "mozilla/TypedEnumBits.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef NS_NO_XPCOM
#  include "nsIFile.h"
#  include "mozilla/AlreadyAddRefed.h"
#endif

// Undo X11/X.h's definition of None
#undef None

namespace mozilla {

enum ArgResult {
  ARG_NONE = 0,
  ARG_FOUND = 1,
  ARG_BAD = 2  // you wanted a param, but there isn't one or you didn't want a
               // param and there is one.
};

template <typename CharT>
inline void RemoveArg(int& argc, CharT** argv) {
  do {
    *argv = *(argv + 1);
    ++argv;
  } while (*argv);

  --argc;
}

namespace internal {

#if 'a' == '\x61'
// Valid option characters must have the same representation in every locale
// (which is true for most of ASCII, barring \x5C and \x7E).
static inline constexpr bool isValidOptionCharacter(char c) {
  // We specifically avoid the use of `islower` here; it's locale-dependent, and
  // may return true for non-ASCII values in some locales.
  return ('0' <= c && c <= '9') || ('a' <= c && c <= 'z') || c == '-';
};

// Convert uppercase to lowercase, locale-insensitively.
static inline constexpr char toLowercase(char c) {
  // We specifically avoid the use of `tolower` here; it's locale-dependent, and
  // may output ASCII values for non-ASCII input (or vice versa) in some
  // locales.
  return ('A' <= c && c <= 'Z') ? char(c | ' ') : c;
};

// Convert a CharT to a char, ensuring that no CharT is mapped to any valid
// option character except the unique CharT naturally corresponding thereto.
template <typename CharT>
static inline constexpr char toNarrow(CharT c) {
  // confirmed to compile down to nothing when `CharT` is `char`
  return (c & static_cast<CharT>(0xff)) == c ? c : 0xff;
};
#else
// The target system's character set isn't even ASCII-compatible. If you're
// porting Gecko to such a platform, you'll have to implement these yourself.
#  error Character conversion functions not implemented for this platform.
#endif

// Case-insensitively compare a string taken from the command-line (`mixedstr`)
// to the text of some known command-line option (`lowerstr`).
// Returns true if mixedstr starts with lowerstr.
template <typename CharT>
static inline bool ArgStartsWith(const CharT* mixedstr, const char* lowerstr) {
  while (*lowerstr) {
    if (!*mixedstr) {
      return false;  // mixedstr is shorter
    }

    // Non-ASCII strings may compare incorrectly depending on the user's locale.
    // Some ASCII-safe characters are also dispermitted for semantic reasons
    // and simplicity.
    if (!isValidOptionCharacter(*lowerstr)) {
      return false;
    }

    if (toLowercase(toNarrow(*mixedstr)) != *lowerstr) {
      return false;  // no match
    }

    ++lowerstr;
    ++mixedstr;
  }

  return true;
}

// Case-insensitively compare a string taken from the command-line (`mixedstr`)
// to the text of some known command-line option (`lowerstr`).
template <typename CharT>
static inline bool strimatch(const char* lowerstr, const CharT* mixedstr) {
  if (ArgStartsWith(mixedstr, lowerstr)) {
    return *(mixedstr + strlen(lowerstr)) == '\0';
  }

  return false;
}

// Given a command-line argument, return Nothing if it isn't structurally a
// command-line option, and Some(<the option text>) if it is.
template <typename CharT>
mozilla::Maybe<const CharT*> ReadAsOption(const CharT* str) {
  if (!str) {
    return Nothing();
  }
  if (*str == '-') {
    str++;
    if (*str == '-') {
      str++;
    }
    return Some(str);
  }
  return Nothing();
}

}  // namespace internal

using internal::ArgStartsWith;
using internal::strimatch;

const wchar_t kCommandLineDelimiter[] = L" \t";

enum class CheckArgFlag : uint32_t {
  None = 0,
  // (1 << 0) Used to be CheckOSInt
  RemoveArg = (1 << 1)  // Remove the argument from the argv array.
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(CheckArgFlag)

/**
 * Check for a commandline flag. If the flag takes a parameter, the
 * parameter is returned in aParam. Flags may be in the form -arg, --arg,
 * /arg (on win32), or in the form -arg=<data> or --arg=<data>.
 *
 * @param aArgc The argc value.
 * @param aArgv The original argv.
 * @param aArg the parameter to check. Must be lowercase.
 * @param aParam if non-null, the -arg <data> or -arg=<data> will be stored in
 * this pointer. This is *not* allocated, but rather a pointer to the argv data
 * or the value after '='.
 * @param aFlags Flags @see CheckArgFlag
 */
template <typename CharT>
inline ArgResult CheckArg(int& aArgc, CharT** aArgv, const char* aArg,
                          const CharT** aParam = nullptr,
                          CheckArgFlag aFlags = CheckArgFlag::RemoveArg) {
  using internal::ReadAsOption;
  MOZ_ASSERT(aArgv && aArg);

  CharT** curarg = aArgv + 1;  // skip argv[0]
  ArgResult ar = ARG_NONE;

  while (*curarg) {
    if (const auto arg = ReadAsOption(*curarg)) {
      if (ArgStartsWith(arg.value(), aArg)) {
        const auto nextChar = arg.value() + strlen(aArg);
        if (*nextChar == 0) {
          // Exact match: --flag or -flag. Remove this from aArgv if the
          // appropriate flag is set.
          if (aFlags & CheckArgFlag::RemoveArg) {
            RemoveArg(aArgc, curarg);
          } else {
            ++curarg;
          }

          if (!aParam) {
            ar = ARG_FOUND;
            break;
          }

          if (*curarg) {
            if (ReadAsOption(*curarg)) {
              return ARG_BAD;
            }

            *aParam = *curarg;

            // This argument also has a value as a separate command line
            // argument so we must remove this from aArgv as well.
            if (aFlags & CheckArgFlag::RemoveArg) {
              RemoveArg(aArgc, curarg);
            }

            ar = ARG_FOUND;
            break;
          }

          return ARG_BAD;
        }

        if (*nextChar == '=') {
          if (aParam) {
            // Here we have an argument with a value, like --foo=bar or
            // -foo=bar. We need to pass the "value" part of it to `aParam` and
            // then remove only one item from `aArgv` if the relevant flag was
            // passed.
            *aParam = nextChar + 1;  // value after '='

            if (aFlags & CheckArgFlag::RemoveArg) {
              RemoveArg(aArgc, curarg);
            }

            ar = ARG_FOUND;
          } else {
            ar = ARG_BAD;
          }

          break;
        }
      }
    }

    ++curarg;
  }

  return ar;
}

template <typename CharT>
inline ArgResult CheckArg(int& aArgc, CharT** aArgv, const char* aArg,
                          std::nullptr_t,
                          CheckArgFlag aFlags = CheckArgFlag::RemoveArg) {
  return CheckArg<CharT>(aArgc, aArgv, aArg,
                         static_cast<const CharT**>(nullptr), aFlags);
}

namespace internal {
// template <typename T>
// constexpr bool IsStringRange =
//     std::convertible_to<std::ranges::range_value_t<T>, const char *>;

template <typename CharT, typename ListT>
// requires IsStringRange<ListT>
bool MatchesAnyOf(CharT const* unknown, ListT const& known) {
  for (const char* k : known) {
    if (strimatch(k, unknown)) {
      return true;
    }
  }
  return false;
}

template <typename CharT, typename ReqContainerT, typename OptContainerT>
// requires IsStringRange<ReqContainerT> && IsStringRange<OptContainerT>
inline bool EnsureCommandlineSafeImpl(int aArgc, CharT** aArgv,
                                      ReqContainerT const& requiredParams,
                                      OptContainerT const& optionalParams) {
  // We expect either no -osint, or the full commandline to be:
  //
  //   app -osint [<optional-param>...] <required-param> <required-argument>
  //
  // Otherwise, we abort to avoid abuse of other command-line handlers from apps
  // that do a poor job escaping links they give to the OS.
  //
  // Note that the above implies that optional parameters do not themselves take
  // arguments. This is a security feature, to prevent the possible injection of
  // additional parameters via such arguments. (See, e.g., bug 384384.)

  static constexpr const char* osintLit = "osint";

  // If "-osint" (or the equivalent) is not present, then this is trivially
  // satisfied.
  if (CheckArg(aArgc, aArgv, osintLit, nullptr, CheckArgFlag::None) !=
      ARG_FOUND) {
    return true;
  }

  // There should be at least 4 items present:
  //    <app name> -osint <required param> <arg>.
  if (aArgc < 4) {
    return false;
  }

  // The first parameter must be osint.
  const auto arg1 = ReadAsOption(aArgv[1]);
  if (!arg1) return false;
  if (!strimatch(osintLit, arg1.value())) {
    return false;
  }
  // Following this is any number of optional parameters, terminated by a
  // required parameter.
  int pos = 2;
  while (true) {
    if (pos >= aArgc) return false;

    auto const arg = ReadAsOption(aArgv[pos]);
    if (!arg) return false;

    if (MatchesAnyOf(arg.value(), optionalParams)) {
      ++pos;
      continue;
    }

    if (MatchesAnyOf(arg.value(), requiredParams)) {
      ++pos;
      break;
    }

    return false;
  }

  // There must be one argument remaining...
  if (pos + 1 != aArgc) return false;
  // ... which must not be another option.
  if (ReadAsOption(aArgv[pos])) {
    return false;
  }

  // Nothing ill-formed was passed.
  return true;
}

// C (and so C++) disallows empty arrays. Rather than require callers to jump
// through hoops to specify an empty optional-argument list, allow either its
// omission or its specification as `nullptr`, and do the hoop-jumping here.
//
// No such facility is provided for requiredParams, which must have at least one
// entry.
template <typename CharT, typename ReqContainerT>
inline bool EnsureCommandlineSafeImpl(int aArgc, CharT** aArgv,
                                      ReqContainerT const& requiredParams,
                                      std::nullptr_t _ = nullptr) {
  struct {
    inline const char** begin() const { return nullptr; }
    inline const char** end() const { return nullptr; }
  } emptyContainer;
  return EnsureCommandlineSafeImpl(aArgc, aArgv, requiredParams,
                                   emptyContainer);
}
}  // namespace internal

template <typename CharT, typename ReqContainerT,
          typename OptContainerT = std::nullptr_t>
inline void EnsureCommandlineSafe(
    int aArgc, CharT** aArgv, ReqContainerT const& requiredParams,
    OptContainerT const& optionalParams = nullptr) {
  if (!internal::EnsureCommandlineSafeImpl(aArgc, aArgv, requiredParams,
                                           optionalParams)) {
    exit(127);
  }
}


// SaveToEnv and EnvHasValue are only available on Windows or when
// MOZILLA_INTERNAL_API is defined
#if defined(MOZILLA_INTERNAL_API) || defined(XP_WIN)

// Save literal putenv string to environment variable.
MOZ_NEVER_INLINE inline void SaveToEnv(const char* aEnvString) {
#  if defined(MOZILLA_INTERNAL_API)
  char* expr = strdup(aEnvString);
  if (expr) {
    PR_SetEnv(expr);
  }

  // We intentionally leak |expr| here since it is required by PR_SetEnv.
  MOZ_LSAN_INTENTIONALLY_LEAK_OBJECT(expr);
#  endif
}

inline bool EnvHasValue(const char* aVarName) {
#  if defined(MOZILLA_INTERNAL_API)
  const char* val = PR_GetEnv(aVarName);
  return val && *val;
#  endif
}

#endif  // end windows/internal_api-only definitions

#ifndef NS_NO_XPCOM
already_AddRefed<nsIFile> GetFileFromEnv(const char* name);
#endif

}  // namespace mozilla

#endif  // mozilla_CmdLineAndEnvUtils_h
