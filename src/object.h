#ifndef OBJECT_H
#define OBJECT_H
// ****************************************************************************
//  object.h                                                      DB48X project
// ****************************************************************************
//
//   File Description:
//
//     The basic RPL object
//
//     An RPL object is a bag of bytes densely encoded using LEB128
//
//     It is important that the base object be empty, sizeof(object) == 1
//
//
//
//
// ****************************************************************************
//   (C) 2022 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the terms outlined in LICENSE.txt
// ****************************************************************************
//   This file is part of DB48X.
//
//   DB48X is free software: you can redistribute it and/or modify
//   it under the terms outlined in the LICENSE.txt file
//
//   DB48X is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// ****************************************************************************
//
// Object encoding:
//
//    RPL objects are encoded using sequence of LEB128 values
//    An LEB128 value is a variable-length encoding with 7 bits per byte,
//    the last byte having its high bit clear. This, values 0-127 are coded
//    as 0-127, values up to 16384 are coded on two bytes, and so on.
//
//    All objects begin with an "identifier" (the type is called id in the code)
//    which uniquely defines the type of the object. Identifiers are defined in
//    the source file called ids.tbl.
//
//    For commands, the object type is all there is to the object. Therefore,
//    most RPL commands consume only one byte in memory. For other objects,
//    there is a "payload" following that identifier. The format of the paylaod
//    is described in the source file for the corresponding object type, but a
//    general guiding principle is that the payload must make it easy to skip
//    the object in memory, notably during garbage collection.
//
// Object handler:
//
//    The type of the object is an index in an object-handler table, so they act
//    either as commands (performing an action when evaluated) or as data types
//    (putting themselves on the runtime stack when evaluated).
//
//    All handlers must respond to a fixed number of "opcodes", which are
//    reserved identifiers in ids.tbl. These opcodes also correspond do
//    user-accessible commands that apply to objects. They include:
//
//    - EVAL:   Evaluates the object
//    - SIZE:   Compute the size of the object
//    - PARSE:  Try to parse an object of the type (see note)
//    - RENDER: Render an object as text
//    - HELP:   Return the name of the help topic associated to the object
//
//    Note: PARSE is the only opcode that does not take an object as input
//
//    The handler is not exactly equivalent to the user command.
//    It may present an internal interface that is more convenient for C code.
//    This approach makes it possible to pass other IDs to an object, for
//    example the "add" operator can delegate the addition of complex numbers
//    to the complex handler by calling the complex handler with 'add'.
//
// Rationale:
//
//    The reason for this design is that the DM42 is very memory-starved
//    (~70K available to DMCP programs), so the focus is on a format for objects
//    that is extremely compact.
//
//    Notably, for size encoding, with only 70K available, the chances of a size
//    exceeding 2 bytes (16384) are exceedingly rare.
//
//    We can also use the lowest opcodes for the most frequently used features,
//    ensuring that 128 of them can be encoded on one byte only. Similarly, all
//    constants less than 128 can be encoded in two bytes only (one for the
//    opcode, one for the value), and all constants less than 16384 are encoded
//    on three bytes.
//
//    Similarly, the design of RPL calls for a garbage collector.
//    The format of objects defined above ensures that all objects are moveable.
//    The garbage collector can therefore be "compacting", moving all live
//    objects at the beginning of memory. This in turns means that each garbage
//    collection cycle gives us a large amount of contiguous memory, but more
//    importantly, that the allocation of new objects is extremely simple, and
//    therefore quite fast.
//
//    The downside is that we can't really use the C++ built-in dyanmic dispatch
//    mechanism (virtual functions), as having a pointer to a vtable would
//    increase the size of the object too much.


#include "types.h"
#include "leb128.h"
#include "recorder.h"

struct runtime;
struct parser;
struct renderer;
struct object;
struct symbol;
struct program;
struct input;
struct text;

RECORDER_DECLARE(object);
RECORDER_DECLARE(parse);
RECORDER_DECLARE(parse_attempts);
RECORDER_DECLARE(render);
RECORDER_DECLARE(eval);
RECORDER_DECLARE(run);
RECORDER_DECLARE(object_errors);

typedef const object  *object_p;
typedef const symbol  *symbol_p;
typedef const program *program_p;
typedef const text    *text_p;


struct object
// ----------------------------------------------------------------------------
//  The basic RPL object
// ----------------------------------------------------------------------------
{
    enum id : unsigned
    // ------------------------------------------------------------------------
    //  Object ID
    // ------------------------------------------------------------------------
    {
#define ID(i)   ID_##i,
#include "ids.tbl"
        NUM_IDS
    };

    object(id i)
    // ------------------------------------------------------------------------
    //  Write the id of the object
    // ------------------------------------------------------------------------
    {
        byte *ptr = (byte *) this;
        leb128(ptr, i);
    }
    ~object() {}


    // ========================================================================
    //
    //   Object command protocol
    //
    // ========================================================================

    enum opcode : unsigned
    // ------------------------------------------------------------------------
    //  The commands that all handlers must deal with
    // ------------------------------------------------------------------------
    {
#define OPCODE(n)       n = ID_##n,
#define ID(n)
#include "ids.tbl"
    };

    enum result
    // ------------------------------------------------------------------------
    //   Common return values for handlers
    // ------------------------------------------------------------------------
    {
        OK    = 0,              // Command ran successfully
        SKIP  = -1,             // Command not for this handler, try next
        ERROR = -2,             // Error processing the command
        WARN  = -3,             // Possible error (if no object succeeds)
    };



    // ========================================================================
    //
    //   Memory management
    //
    // ========================================================================

    static size_t required_memory(id i)
    // ------------------------------------------------------------------------
    //  Compute the amount of memory required for an object
    // ------------------------------------------------------------------------
    {
        return leb128size(i);
    }

    id type() const
    // ------------------------------------------------------------------------
    //   Return the type of the object
    // ------------------------------------------------------------------------
    {
        byte *ptr = (byte *) this;
        id ty = (id) leb128(ptr);
        if (ty > NUM_IDS)
            object_error(ty, this);
        return ty;
    }

    size_t size(runtime &rt = RT) const
    // ------------------------------------------------------------------------
    //  Compute the size of the object by calling the handler with SIZE
    // ------------------------------------------------------------------------
    {
        return (size_t) run(SIZE, rt);
    }

    object_p skip(runtime &rt = RT) const
    // ------------------------------------------------------------------------
    //  Return the pointer to the next object in memory by skipping its size
    // ------------------------------------------------------------------------
    {
        return this + size(rt);
    }

    byte * payload() const
    // ------------------------------------------------------------------------
    //  Return the object's payload, i.e. first byte after ID
    // ------------------------------------------------------------------------
    {
        byte *ptr = (byte *) this;
        leb128(ptr);            // Skip ID
        return ptr;
    }

    static void object_error(id type, const object *ptr);
    // ------------------------------------------------------------------------
    //   Report an error e.g. with with an object type
    // ------------------------------------------------------------------------



    // ========================================================================
    //
    //    High-level functions on objects
    //
    // ========================================================================

    result evaluate(runtime &rt = RT) const
    // ------------------------------------------------------------------------
    //  Evaluate an object by calling the handler
    // ------------------------------------------------------------------------
    {
        record(eval, "Evaluating %+s %p", name(), this);
        return (result) run(EVAL, rt);
    }

    result execute(runtime &rt = RT) const
    // ------------------------------------------------------------------------
    //   Execute the object, i.e. run programs and equations
    // ------------------------------------------------------------------------
    {
        record(eval, "Evaluating %+s %p", name(), this);
        return (result) run(EXEC, rt);
    }

    size_t render(char *output, size_t length, runtime &rt = RT) const;
    // ------------------------------------------------------------------------
    //   Render the object into a buffer
    // ------------------------------------------------------------------------

    cstring render(bool edit = false, runtime &rt = RT) const;
    // ------------------------------------------------------------------------
    //   Render the object into the scratchpad
    // ------------------------------------------------------------------------

    cstring edit(runtime &rt = RT) const;
    // ------------------------------------------------------------------------
    //   Render the object into the scratchpad, then move into the editor
    // ------------------------------------------------------------------------

    text_p as_text(bool equation = false, runtime &rt = RT) const;
    // ------------------------------------------------------------------------
    //   Return the object as text
    // ------------------------------------------------------------------------

    symbol_p as_symbol(runtime &rt = RT) const
    // ------------------------------------------------------------------------
    //   Return the object as text
    // ------------------------------------------------------------------------
    {
        return symbol_p(as_text(true, rt));
    }

    result insert(input *Input, runtime &rt = RT) const
    // ------------------------------------------------------------------------
    //   Insert in the editor at cursor position
    // ------------------------------------------------------------------------
    {
        return (object::result) run(INSERT, rt, Input);
    }


    static object_p parse(utf8 source, size_t &size, runtime &rt = RT);
    // ------------------------------------------------------------------------
    //  Try parsing the object as a top-level temporary
    // ------------------------------------------------------------------------


    utf8 help(runtime &rt = RT) const
    // ------------------------------------------------------------------------
    //   Return the help topic for the given object
    // ------------------------------------------------------------------------
    {
        return (utf8) run(HELP, rt);
    }

    static cstring name(opcode op)
    // ------------------------------------------------------------------------
    //   Return the name for a given ID
    // ------------------------------------------------------------------------
    {
        return cstring(name(id(op)));
    }

    static cstring name(result r)
    // ------------------------------------------------------------------------
    //    Convenience function for the name of results
    // ------------------------------------------------------------------------
    {
        switch (r)
        {
        case OK:        return "OK";
        case SKIP:      return "SKIP";
        case ERROR:     return "ERROR";
        case WARN:      return "WARN";
        }
        return "<Unknown>";
    }


    static utf8 name(id i)
    // ------------------------------------------------------------------------
    //   Return the name for a given ID
    // ------------------------------------------------------------------------
    {
        return utf8(i < NUM_IDS ? id_name[i] : "<invalid ID>");
    }


    static utf8 fancy(id i)
    // ------------------------------------------------------------------------
    //   Return the fancy name for a given ID
    // ------------------------------------------------------------------------
    {
        return utf8(i < NUM_IDS ? fancy_name[i] : "<Invalid ID>");
    }


    utf8 name() const
    // ------------------------------------------------------------------------
    //   Return the name for the current object
    // ------------------------------------------------------------------------
    {
        return name(type());
    }


    utf8 fancy() const
    // ------------------------------------------------------------------------
    //   Return the fancy name for the current object
    // ------------------------------------------------------------------------
    {
        return fancy(type());
    }


    unicode marker() const
    // ------------------------------------------------------------------------
    //   Marker in menus
    // ------------------------------------------------------------------------
    {
        return (unicode) run(MENU_MARKER);
    }



    // ========================================================================
    //
    //    Attributes of objects
    //
    // ========================================================================

    static bool is_integer(id ty)
    // -------------------------------------------------------------------------
    //   Check if a type is an integer
    // -------------------------------------------------------------------------
    {
        return ty >= FIRST_INTEGER_TYPE && ty <= LAST_INTEGER_TYPE;
    }


    bool is_integer() const
    // -------------------------------------------------------------------------
    //   Check if an object is an integer
    // -------------------------------------------------------------------------
    {
        return is_integer(type());
    }


    static bool is_decimal(id ty)
    // -------------------------------------------------------------------------
    //   Check if a type is a decimal
    // -------------------------------------------------------------------------
    {
        return ty >= FIRST_DECIMAL_TYPE && ty <= LAST_DECIMAL_TYPE;
    }


    bool is_decimal() const
    // -------------------------------------------------------------------------
    //   Check if an object is a decimal
    // -------------------------------------------------------------------------
    {
        return is_decimal(type());
    }


    static  bool is_real(id ty)
    // -------------------------------------------------------------------------
    //   Check if a type is a real number
    // -------------------------------------------------------------------------
    {
        return ty >= FIRST_REAL_TYPE && ty <= LAST_REAL_TYPE;
    }


    bool is_real() const
    // -------------------------------------------------------------------------
    //   Check if an object is a real number
    // -------------------------------------------------------------------------
    {
        return is_real(type());
    }


    static bool is_command(id ty)
    // ------------------------------------------------------------------------
    //    Check if a type denotes a command
    // ------------------------------------------------------------------------
    {
        return ty >= FIRST_COMMAND && ty <= LAST_COMMAND;
    }


    bool is_command() const
    // ------------------------------------------------------------------------
    //   Check if an object is a command
    // ------------------------------------------------------------------------
    {
        return is_command(type());
    }


    static bool is_symbolic(id ty)
    // ------------------------------------------------------------------------
    //    Check if a type denotes a symbolic argument (symbol, equation, number)
    // ------------------------------------------------------------------------
    {
        return ty >= FIRST_SYMBOLIC_TYPE && ty <= LAST_SYMBOLIC_TYPE;
    }


    bool is_symbolic() const
    // ------------------------------------------------------------------------
    //   Check if an object is a symbolic argument
    // ------------------------------------------------------------------------
    {
        return is_symbolic(type());
    }


    static bool is_strictly_symbolic(id ty)
    // ------------------------------------------------------------------------
    //    Check if a type denotes a symbol or equation
    // ------------------------------------------------------------------------
    {
        return ty == ID_symbol || ty == ID_equation;
    }


    bool is_strictly_symbolic() const
    // ------------------------------------------------------------------------
    //   Check if an object is a symbol or equation
    // ------------------------------------------------------------------------
    {
        return is_strictly_symbolic(type());
    }


    static bool is_algebraic(id ty)
    // ------------------------------------------------------------------------
    //    Check if a type denotes an algebraic function
    // ------------------------------------------------------------------------
    {
        return ty >= FIRST_ALGEBRAIC && ty <= LAST_ALGEBRAIC;
    }


    bool is_algebraic() const
    // ------------------------------------------------------------------------
    //   Check if an object is an algebraic function
    // ------------------------------------------------------------------------
    {
        return is_algebraic(type());
    }


    intptr_t arity() const
    // ------------------------------------------------------------------------
    //   Return the arity for arithmetic operators
    // ------------------------------------------------------------------------
    {
        return run(ARITY);
    }


    intptr_t precedence() const
    // ------------------------------------------------------------------------
    //   Return the arity for arithmetic operators
    // ------------------------------------------------------------------------
    {
        return run(PRECEDENCE);
    }


    template<typename Obj> const Obj *as() const
    // ------------------------------------------------------------------------
    //   Type-safe cast (note: only for exact type match)
    // ------------------------------------------------------------------------
    {
        if (type() == Obj::static_type())
            return (const Obj *) this;
        return nullptr;
    }


    template<typename Obj, typename Derived> const Obj *as() const
    // ------------------------------------------------------------------------
    //   Type-safe cast (note: only for exact type match)
    // ------------------------------------------------------------------------
    {
        id t = type();
        if (t >= Obj::static_type() && t <= Derived::static_type())
            return (const Obj *) this;
        return nullptr;
    }


    symbol_p as_name() const;
    // ------------------------------------------------------------------------
    //    Return object as a name
    // ------------------------------------------------------------------------


    int as_truth() const;
    // ------------------------------------------------------------------------
    //   Return 0 or 1 if this is a logical value, -1 and type error otherwise
    // ------------------------------------------------------------------------



    // ========================================================================
    //
    //    Low-level function dispatch
    //
    // ========================================================================

    static intptr_t run(id       type,
                        opcode   op  = EVAL,
                        runtime &rt  = RT,
                        void    *arg = nullptr)
    // ------------------------------------------------------------------------
    //  Run a command without an object
    // ------------------------------------------------------------------------
    {
        if (type >= NUM_IDS)
        {
            record(object_errors, "Static run op %+s with id %u, max %u",
                   name(op), type, NUM_IDS);
            object_error(type, (object_p) "Static");
            return ERROR;
        }
        record(run, "Static run %+s cmd %+s", name(type), name(op));
        return handler[type](rt, op, arg, nullptr, nullptr);
    }

    intptr_t run(opcode op, runtime &rt = RT, void *arg = nullptr) const
    // ------------------------------------------------------------------------
    //  Run an arbitrary command on the object
    // ------------------------------------------------------------------------
    {
        byte *ptr = (byte *) this;
        id type = (id) leb128(ptr); // Don't use type() to update payload
        if (type >= NUM_IDS)
        {
            record(object_errors,
                   "Dynamic run op %+s at %p with id %u, max %u",
                   name(op), this, type, NUM_IDS);
            object_error(type, this);
            return -1;
        }
        record(run, "Dynamic run %+s op %+s", name(type), name(op));
        return handler[type](rt, op, arg, this, (object_p ) ptr);
    }

    template <typename Obj>
    static intptr_t run(opcode     op,
                        void      *arg     = nullptr,
                        const Obj *obj     = nullptr,
                        object_p   payload = nullptr,
                        runtime   &rt      = Obj::RT)
    // -------------------------------------------------------------------------
    //   Directly call the object handler for a type (no indirection)
    // -------------------------------------------------------------------------
    {
        record(run, "Direct %+s op %+s", name(Obj::static_type()), name(op));
        return Obj::object_handler(rt, op, arg, obj, payload);
    }

    template <typename Obj>
    static intptr_t run()
    // -------------------------------------------------------------------------
    //   Directly call the object evaluate (no indirection)
    // -------------------------------------------------------------------------
    {
        record(run, "Evaluate %+s", name(Obj::static_type()));
        return Obj::evaluate();
    }

protected:
#define OBJECT_PARSER(type)                                             \
    static result object_parser(parser UNUSED &p, runtime & UNUSED rt = RT)

#define OBJECT_PARSER_BODY(type)                            \
    object::result type::object_parser(parser UNUSED &p, runtime &UNUSED rt)

#define OBJECT_PARSER_ARG()     (*((parser *) arg))

#define OBJECT_RENDERER(type)                           \
    intptr_t object_renderer(renderer &r, runtime &UNUSED rt = RT) const

#define OBJECT_RENDERER_BODY(type)                              \
    intptr_t type::object_renderer(renderer &r, runtime &UNUSED rt) const

#define OBJECT_RENDERER_ARG()   (*((renderer *) arg))

    // The actual work is done here
#define OBJECT_HANDLER_NO_ID(type)                                    \
    static intptr_t object_handler(runtime    &UNUSED rt,             \
                                   opcode      UNUSED op,             \
                                   void       *UNUSED arg,            \
                                   const type *UNUSED obj,            \
                                   object_p    UNUSED payload)

#define OBJECT_HANDLER(type)                            \
    static id static_type() { return ID_##type; }       \
    OBJECT_HANDLER_NO_ID(type)

#define OBJECT_HANDLER_BODY(type)                                       \
    intptr_t type::object_handler(runtime      &UNUSED rt,              \
                                  opcode        UNUSED op,              \
                                  void         *UNUSED arg,             \
                                  const type   *UNUSED obj,             \
                                  object_p      UNUSED payload)

  // The default object handlers
  OBJECT_PARSER(object);
  OBJECT_RENDERER(object);
  OBJECT_HANDLER_NO_ID(object);


#define DELEGATE(base)                                          \
    base::object_handler(rt, op, arg, (base *) obj, payload)

    template <typename T, typename U>
    static intptr_t ptrdiff(T *t, U *u)
    {
        return (byte *) t - (byte *) u;
    }


protected:
    typedef intptr_t (*handler_fn)(runtime &rt,
                                   opcode op, void *arg,
                                   object_p obj, object_p payload);
    static const handler_fn handler[NUM_IDS];
    static const cstring    id_name[NUM_IDS];
    static const cstring    fancy_name[NUM_IDS];
    static runtime         &RT;
};


#endif // OBJECT_H
