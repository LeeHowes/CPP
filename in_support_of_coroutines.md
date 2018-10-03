| | |
| --------|-------|
| Document: | Dxxxx |
| Date: | October 8, 2018 |
| Audience: | SG1 |
| Authors: | Lee Howes &lt;lwh@fb.com&gt;<br/>Eric Niebler &lt;eniebler@fb.com&gt;<br/>Lewis Baker &lt;lbaker@fb.com&gt; |

# Summary
We believe that we should move forward with merging the Coroutines TS into the IS for C++20. While there is some possibility that the Core Coroutines [P1063] work may show some advantages in some areas it is far from clear, and has a long timeline to realise that clarify given the implementation experience we have with the TS already.

# Facebook's experience
Coroutines are increasingly important to Facebook's long term planning, and likely to the standard as a whole.

We are supporting work on an implementation of coroutines in [GCC], improvements to the implementation in LLVM both in terms of optimisations and bug fixes as they come along, developing library types and implementing production libraries based on the TS definition.
Lewis Baker has significant experience of implementing functionality using the coroutines TS definitions in the [cppcoro] library and is using this experience to build a variety of coroutine support types in the [folly] coro library.

We have had experience with the other programming languages we use of transitioning millions of lines of code to the `async`/`await` style and have become convinced that this improves programmer productivity and code safety significantly.
There is enormous advantage in the way libraries that return `Awaitable` types force callers to `await` them, turning codebases naturally into highly asynchronous code structures without significant effort. In addition, it leads to code structures that are asynchronous but safer than the alternatives - it is both harder to make mistakes around lifetime of references, and easier to see those mistakes during code review or bug hunting.

Compared to library-based fibers code we see a different set of advantages around code safety. With the coroutines TS we can enforce in the type system how execution contexts propagate through a call graph, have tight control over where tasks run and what happens when they complete, and relatively easily audit code for misuse of synchronisation primitives. Making the information available in the type system also makes AST-based auditing and code transformation a much more powerful and less error-prone tool.

While there are known gaps in the standard, we see a path to filling these gaps that we are comfortable with and that need not affect our implementation strategy.

# A call to progress
It has been said frequently that the best is the enemy of the good. At the present time, the flaws the core coroutines proposal tries to fix are not entirely clear, and worse not clearly fixed. We could go down the path of waiting, and hoping that that proposal is fleshed out enough to be convincing, is used to implement everything that has been implemented using the TS functionality and shown to be on parity in all other areas, as well as fixing the flaws. This is a long road, and one that may still not take us anywhere.

The community needs to be able to run and not walk in the direction of coroutines, with the huge usability and performance improvements they bring.
We can fight over libraries, and that is fine, we can implement libraries independently until we agree on a reasonable subset. We will be in a true mess if we fight over libraries based on differing fundamentals.

As one example, we believe that the Networking TS in its current form does not optimally support coroutines. We can change this, but if we are to propose that modification we need to know what coroutine language core we are building on. Landing the language feature gives us that fundamental primitives so that we can investigate fully supporting coroutines throughout the standard library in C++23.

For that reason, we should merge the coroutines TS. We should commit to a path, allow the library experimentation on a known core, and move briskly in the direction of improving our asynchronous code through coroutines, and we believe improving our error handling code as well. At Facebook we have decided that we cannot wait and we will simply risk a rewrite if necessary. At the same time, we would prefer to avoid that rewrite risk and our implementation experience makes us happy with the current direction.

[cppcoro]: https://github.com/lewissbaker/cppcoro
[folly]: https://github.com/facebook/folly/tree/master/folly/experimental/coro
[P1063]: http://wg21.link/P1063
[GCC]: https://gcc.gnu.org/wiki/cxx-coroutines
