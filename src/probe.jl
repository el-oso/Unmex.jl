# Best-effort introspection of an opaque MEX. A `.mex*` exports only `mexFunction` and
# carries no signature metadata, so `probe` recovers what it can by (1) sweeping input
# arities and reporting each outcome — Unmex turns the MEX's own `mexErrMsgIdAndTxt` into a
# catchable error — and (2) scanning the binary for embedded contract strings (the author's
# error texts often spell out the expected arity/types).
#
# Caveat: probing *calls the MEX for real*. A well-behaved MEX validates `nrhs`/types before
# touching `prhs`, so wrong guesses come back as catchable errors; a MEX that dereferences
# `prhs[0]` without checking could crash the process (the `setjmp` shim catches a longjmp,
# not a segfault). Treat results as discovery, not a guaranteed signature: a MEX may be
# polymorphic (branching on count, type, or a leading command string).

function _describe(@nospecialize(x))::String
    if x isa AbstractArray
        return string(eltype(x), " ", isempty(x) ? "(empty)" : join(size(x), "×"))
    elseif x === nothing
        return "nothing"
    else
        return string(typeof(x), "(", x, ")")
    end
end

_oneline(e)::String = first(split(sprint(showerror, e), '\n'))

# Extract printable-ASCII runs from the binary, keep the ones that look like an error id
# (`a:b[:c…]`) or a human message mentioning a signature keyword.
function _scan_contract_strings(path::AbstractString; minlen::Int = 4, limit::Int = 40)
    data = read(path)
    runs = String[]
    buf = IOBuffer()
    for b in data
        if 0x20 <= b <= 0x7e
            write(buf, b)
        else
            s = String(take!(buf))
            length(s) >= minlen && push!(runs, s)
        end
    end
    s = String(take!(buf))
    length(s) >= minlen && push!(runs, s)

    idpat = r"^[A-Za-z]\w*(:[A-Za-z]\w*){1,}$"
    kwpat = r"input|output|argument|\barg\b|require|expect|must|scalar|matrix|vector|real|complex|double|single|logical|string|char|nrhs|nlhs|nargin|nargout"i
    keep = String[]
    seen = Set{String}()
    for s in runs
        (occursin(idpat, s) || (occursin(' ', s) && occursin(kwpat, s))) || continue
        s in seen && continue
        push!(seen, s)
        push!(keep, s)
        length(keep) >= limit && break
    end
    return keep
end

"""
    probe([io=stdout], mex; max_nargs=3, sample=[1.0 2.0; 3.0 4.0], types=true, scan=true, call=true)

Best-effort discovery of how to call an opaque MEX. `mex` may be a [`MexFunction`](@ref) or
a path. Prints a report with three parts:

  * **arity sweep** — calls with `0:max_nargs` copies of `sample`, reporting the returned
    type/size or the MEX's own error for each count;
  * **type probe** — at the smallest arity that worked, tries a few common argument types;
  * **strings** — `mexErrMsgIdAndTxt`-style texts found in the binary (often the contract).

A MEX carries no signature metadata and may be polymorphic, so this is discovery, not a
complete specification. The arity/type sweeps *invoke the MEX* — pass `call=false` to skip
them and only scan the binary (safe for a MEX you can't or won't execute, e.g. one that
embeds its own runtime, or an untrusted binary). See the note in `src/probe.jl`.
"""
function probe(
        io::IO,
        mex::MexFunction;
        max_nargs::Integer = 3,
        sample = [1.0 2.0; 3.0 4.0],
        types::Bool = true,
        scan::Bool = true,
        call::Bool = true,
    )
    println(io, "Probing ", repr(basename(mex.path)), "  (entry point: mexFunction)")

    if !call
        println(io, "\n(call=false: not invoking the MEX — scan only)")
    else
        println(io, "\nArity sweep (", summary(sample), " per argument):")
        okarity = Int[]
        for n in 0:Int(max_nargs)
            args = ntuple(_ -> sample, n)
            try
                r = Unmex.call(mex, args...)
                push!(okarity, n)
                println(io, "  nargs=$n → OK, returns ", _describe(r))
            catch e
                println(io, "  nargs=$n → ", _oneline(e))
            end
        end

        if types && !isempty(okarity)
            n = first(okarity)
            println(io, "\nType probe at nargs=$n:")
            for v in Any[1.0, Int32[1 2 3], "hello", [true false true]]
                args = ntuple(_ -> v, n)
                try
                    r = Unmex.call(mex, args...)
                    println(io, "  ", rpad(_describe(v), 24), " → OK, returns ", _describe(r))
                catch e
                    println(io, "  ", rpad(_describe(v), 24), " → ", _oneline(e))
                end
            end
        end
    end

    if scan
        msgs = _scan_contract_strings(mex.path)
        println(io, "\nEmbedded contract strings (", length(msgs), "):")
        isempty(msgs) && println(io, "  (none found)")
        for s in msgs
            println(io, "  ", s)
        end
    end
    return nothing
end

probe(mex::MexFunction; kw...) = probe(stdout, mex; kw...)
probe(io::IO, path::AbstractString; kw...) = probe(io, open_mex(path); kw...)
probe(path::AbstractString; kw...) = probe(stdout, open_mex(path); kw...)
