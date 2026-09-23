```elixir
# send and receive messages, like in elixir
send(self, {msg: "hi"})

# finds the first message which matches {msg: "hi"}
receive ({msg: "hi"}) do
    println("hello")
end

actor Stack(init) do
    # get functions can not alter the stack state
    get head(state)
        state[0]

    # Runs synchronously, like normal functions. Returns the current actor
    fun push(state, item)
        [item, ..state] # new state

    # This function returns a special record (marked by the `@` sign) with keys reply and state. `reply` is returned to the caller,
    # while `state` is set as the actor internal state. Any other record key is a compile error. If any of the keys are omitted,
    # nil is returned, i.e. @{ state: something } is the same as @{ reply: nil, state: something }.
    fun pop(state) do
        [head, ..tail] = state
        @{ reply: head, state: tail }
    end

    # `box`functions run asynchronously. Here, a `caller` parameter is defined to be able to send messages back to the caller
    box popmany(state, n, caller) do
        fun popn[caller](lst, n)
        | (lst, 0) do lst
        | (lst, n) do
            [head, ..tail] = lst
            send(caller, {popn: head})
            popn(tail, n - 1)
        end

        popn(state, n)
    end

    # runs async
    box pushmany(state, items) do
        fun pushmany_impl(state, items)
        | (state, []) do state
        | (state, [o, ..rest]) do pushmany_impl([o, ..state], rest)
        end

        pushmany_impl(state, items)
    end
end

stack = Stack([])
Stack::push(stack, 1)
Stack::pushmany(stack, [2, 3])
Stack::head(stack) |> println() # 3. Forces stack to handle its messages

n = 2
Stack::popmany(stack, n)
#read the items which were popped
(fun read(n)
| (0) do nil
| (n) do
    receive ({popn: item}) do
        print_vals(["Removed ", item])
    end
    read(n - 1)
end)(n)
```
