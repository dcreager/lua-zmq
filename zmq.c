/*
 * Copyright (c) 2010 Aleksey Yeschenko <aleksey@yeschenko.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"

#include <zmq.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>

#define MT_ZMQ_CONTEXT "MT_ZMQ_CONTEXT"
#define MT_ZMQ_MESSAGE "MT_ZMQ_MESSAGE"
#define MT_ZMQ_SOCKET  "MT_ZMQ_SOCKET"

typedef struct { void *ptr; } zmq_ptr;

static int Lzmq_version(lua_State *L)
{
    int major, minor, patch;

    zmq_version(&major, &minor, &patch);

    lua_createtable(L, 3, 0);

    lua_pushinteger(L, 1);
    lua_pushinteger(L, major);
    lua_settable(L, -3);

    lua_pushinteger(L, 2);
    lua_pushinteger(L, minor);
    lua_settable(L, -3);

    lua_pushinteger(L, 3);
    lua_pushinteger(L, patch);
    lua_settable(L, -3);

    return 1;
}

#define zmq_return_error() \
    const char *error = zmq_strerror(zmq_errno()); \
    lua_pushnil(L); \
    lua_pushlstring(L, error, strlen(error)); \
    return 2

static int Lzmq_init(lua_State *L)
{
    int io_threads = luaL_checkint(L, 1);
    zmq_ptr *ctx = lua_newuserdata(L, sizeof(zmq_ptr));
    ctx->ptr = zmq_init(io_threads);

    if (!ctx->ptr) {
        zmq_return_error();
    }

    luaL_getmetatable(L, MT_ZMQ_CONTEXT);
    lua_setmetatable(L, -2);

    return 1;
}

static int Lzmq_term(lua_State *L)
{
    zmq_ptr *ctx = luaL_checkudata(L, 1, MT_ZMQ_CONTEXT);

    if(ctx->ptr != NULL) {
        if(zmq_term(ctx->ptr) == 0) {
            ctx->ptr = NULL;
        } else {
            zmq_return_error();
        }
    }

    lua_pushboolean(L, 1);

    return 1;
}

static int Lzmq_socket(lua_State *L)
{
    zmq_ptr *ctx = luaL_checkudata(L, 1, MT_ZMQ_CONTEXT);
    int type = luaL_checkint(L, 2);

    zmq_ptr *s = lua_newuserdata(L, sizeof(zmq_ptr));

    s->ptr = zmq_socket(ctx->ptr, type);

    if (!s->ptr) {
        zmq_return_error();
    }

    luaL_getmetatable(L, MT_ZMQ_SOCKET);
    lua_setmetatable(L, -2);

    return 1;
}

static int Lzmq_close(lua_State *L)
{
    zmq_ptr *s = luaL_checkudata(L, 1, MT_ZMQ_SOCKET);

    if(s->ptr != NULL) {
        if(zmq_close(s->ptr) == 0) {
            s->ptr = NULL;
        } else {
            zmq_return_error();
        }
    }

    lua_pushboolean(L, 1);

    return 1;
}

static int Lzmq_setsockopt(lua_State *L)
{
    zmq_ptr *s = luaL_checkudata(L, 1, MT_ZMQ_SOCKET);
    int option = luaL_checkint(L, 2);

    int rc = 0;

    switch (option) {
    case ZMQ_SWAP:
    case ZMQ_RATE:
    case ZMQ_RECOVERY_IVL:
    case ZMQ_MCAST_LOOP:
        {
            int64_t optval = (int64_t) luaL_checklong(L, 3);
            rc = zmq_setsockopt(s->ptr, option, (void *) &optval, sizeof(int64_t));
        }
        break;
    case ZMQ_IDENTITY:
    case ZMQ_SUBSCRIBE:
    case ZMQ_UNSUBSCRIBE:
        {
            size_t optvallen;
            const char *optval = luaL_checklstring(L, 3, &optvallen);
            rc = zmq_setsockopt(s->ptr, option, (void *) optval, optvallen);
        }
        break;
    case ZMQ_HWM:
    case ZMQ_AFFINITY:
    case ZMQ_SNDBUF:
    case ZMQ_RCVBUF:
        {
            uint64_t optval = (uint64_t) luaL_checklong(L, 3);
            rc = zmq_setsockopt(s->ptr, option, (void *) &optval, sizeof(uint64_t));
        }
        break;
    default:
        rc = -1;
        errno = EINVAL;
    }

    if (rc != 0) {
        zmq_return_error();
    }

    lua_pushboolean(L, 1);

    return 1;
}

static int Lzmq_getsockopt(lua_State *L)
{
    zmq_ptr *s = luaL_checkudata(L, 1, MT_ZMQ_SOCKET);
    int option = luaL_checkint(L, 2);

    size_t optvallen;

    int rc = 0;

    switch (option) {
    case ZMQ_SWAP:
    case ZMQ_RATE:
    case ZMQ_RECOVERY_IVL:
    case ZMQ_MCAST_LOOP:
    case ZMQ_RCVMORE:
        {
            int64_t optval;
            optvallen = sizeof(int64_t);
            rc = zmq_getsockopt(s->ptr, option, (void *) &optval, &optvallen);
            if (rc == 0) {
                lua_pushinteger(L, (lua_Integer) optval);
                return 1;
            }
        }
        break;
    case ZMQ_IDENTITY:
        {
            char id[256];
            memset((void *)id, '\0', 256);
            optvallen = 256;
            rc = zmq_getsockopt(s->ptr, option, (void *)id, &optvallen);
            id[255] = '\0';
            if (rc == 0) {
                lua_pushstring(L, id);
                return 1;
            }
        }
        break;
    case ZMQ_HWM:
    case ZMQ_AFFINITY:
    case ZMQ_SNDBUF:
    case ZMQ_RCVBUF:
        {
            uint64_t optval;
            optvallen = sizeof(uint64_t);
            rc = zmq_getsockopt(s->ptr, option, (void *) &optval, &optvallen);
            if (rc == 0) {
                lua_pushinteger(L, (lua_Integer) optval);
                return 1;
            }
        }
        break;
    default:
        rc = -1;
        errno = EINVAL;
    }

    if (rc != 0) {
        zmq_return_error();
    }

    lua_pushboolean(L, 1);

    return 1;
}

static int Lzmq_bind(lua_State *L)
{
    zmq_ptr *s = luaL_checkudata(L, 1, MT_ZMQ_SOCKET);
    const char *addr = luaL_checkstring(L, 2);

    if (zmq_bind(s->ptr, addr) != 0) {
        zmq_return_error();
    }

    lua_pushboolean(L, 1);

    return 1;
}

static int Lzmq_connect(lua_State *L)
{
    zmq_ptr *s = luaL_checkudata(L, 1, MT_ZMQ_SOCKET);
    const char *addr = luaL_checkstring(L, 2);

    if (zmq_connect(s->ptr, addr) != 0) {
        zmq_return_error();
    }

    lua_pushboolean(L, 1);

    return 1;
}

static int Lzmq_send(lua_State *L)
{
    zmq_ptr *s = luaL_checkudata(L, 1, MT_ZMQ_SOCKET);
    size_t msg_size;
    const char *data = luaL_checklstring(L, 2, &msg_size);
    int flags = luaL_optint(L, 3, 0);

    zmq_msg_t msg;
    if(zmq_msg_init_size(&msg, msg_size) != 0) {
        zmq_return_error();
    }
    memcpy(zmq_msg_data(&msg), data, msg_size);

    int rc = zmq_send(s->ptr, &msg, flags);

    if(zmq_msg_close(&msg) != 0) {
        zmq_return_error();
    }

    if (rc != 0) {
        zmq_return_error();
    }

    lua_pushboolean(L, 1);

    return 1;
}

static int Lzmq_send_raw(lua_State *L)
{
    zmq_ptr *s = luaL_checkudata(L, 1, MT_ZMQ_SOCKET);
    zmq_msg_t *msg = luaL_checkudata(L, 2, MT_ZMQ_MESSAGE);
    int flags = luaL_optint(L, 3, 0);

    int rc = zmq_send(s->ptr, msg, flags);

    if (rc != 0) {
        zmq_return_error();
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int Lzmq_recv(lua_State *L)
{
    zmq_ptr *s = luaL_checkudata(L, 1, MT_ZMQ_SOCKET);
    int flags = luaL_optint(L, 2, 0);

    zmq_msg_t msg;
    if(zmq_msg_init(&msg) != 0) {
        zmq_return_error();
    }

    if(zmq_recv(s->ptr, &msg, flags) != 0) {
        // Best we can do in this case is try to close and hope for the best.
        zmq_msg_close(&msg);
        zmq_return_error();
    }

    lua_pushlstring(L, zmq_msg_data(&msg), zmq_msg_size(&msg));

    if(zmq_msg_close(&msg) != 0) {
        // Above string will be poped from the stack by the normalising code
        // upon sucessful return.
        zmq_return_error();
    }

    return 1;
}

static int Lzmq_recv_raw(lua_State *L)
{
    zmq_ptr *s = luaL_checkudata(L, 1, MT_ZMQ_SOCKET);
    zmq_msg_t *msg = luaL_checkudata(L, 2, MT_ZMQ_MESSAGE);
    int flags = luaL_optint(L, 3, 0);

    if (zmq_recv(s->ptr, msg, flags) != 0) {
        // Best we can do in this case is try to close and hope for the best.
        zmq_msg_close(&msg);
        zmq_return_error();
    }

    return 1;
}

static int Lzmq_Message(lua_State *L)
{
    zmq_msg_t *msg = lua_newuserdata(L, sizeof(zmq_msg_t));
    zmq_msg_init(msg);
    luaL_getmetatable(L, MT_ZMQ_MESSAGE);
    lua_setmetatable(L, -2);
    return 1;
}

static int Lzmq_msg_data(lua_State *L)
{
    zmq_msg_t *msg = luaL_checkudata(L, 1, MT_ZMQ_MESSAGE);
    lua_pushlstring(L, zmq_msg_data(msg), zmq_msg_size(msg));
    return 1;
}

static int Lzmq_msg_gc(lua_State *L)
{
    zmq_msg_t *msg = luaL_checkudata(L, 1, MT_ZMQ_MESSAGE);
    zmq_msg_close(msg);
    return 0;
}

static const luaL_reg zmqlib[] = {
    {"version",    Lzmq_version},
    {"init",       Lzmq_init},
    {"Message",    Lzmq_Message},
    {NULL,         NULL}
};

static const luaL_reg ctxmethods[] = {
    {"__gc",       Lzmq_term},
    {"term",       Lzmq_term}, 
    {"socket",     Lzmq_socket},
    {NULL,         NULL}
};

static const luaL_reg sockmethods[] = {
    {"__gc",    Lzmq_close},
    {"close",   Lzmq_close},
    {"setopt",  Lzmq_setsockopt},
    {"getopt",  Lzmq_getsockopt},
    {"bind",    Lzmq_bind},
    {"connect", Lzmq_connect},
    {"send",    Lzmq_send},
    {"send_raw", Lzmq_send_raw},
    {"recv",    Lzmq_recv},
    {"recv_raw", Lzmq_recv_raw},
    {NULL,      NULL}
};

static const luaL_reg msgmethods[] = {
    {"__gc",       Lzmq_msg_gc},
    {"data",       Lzmq_msg_data},
    {NULL,         NULL}
};

#define set_zmq_const(s) lua_pushinteger(L,ZMQ_##s); lua_setfield(L, -2, #s);

LUALIB_API int luaopen_zmq(lua_State *L)
{
    /* context metatable. */
    luaL_newmetatable(L, MT_ZMQ_CONTEXT);
    lua_createtable(L, 0, sizeof(ctxmethods) / sizeof(luaL_reg) - 1);
    luaL_register(L, NULL, ctxmethods);
    lua_setfield(L, -2, "__index");

    /* socket metatable. */
    luaL_newmetatable(L, MT_ZMQ_SOCKET);
    lua_createtable(L, 0, sizeof(sockmethods) / sizeof(luaL_reg) - 1);
    luaL_register(L, NULL, sockmethods);
    lua_setfield(L, -2, "__index");

    /* message metatable. */
    luaL_newmetatable(L, MT_ZMQ_MESSAGE);
    lua_createtable(L, 0, sizeof(msgmethods) / sizeof(luaL_reg) - 1);
    luaL_register(L, NULL, msgmethods);
    lua_setfield(L, -2, "__index");

    luaL_register(L, "zmq", zmqlib);

    set_zmq_const(PAIR);
    set_zmq_const(PUB);
    set_zmq_const(SUB);
    set_zmq_const(REQ);
    set_zmq_const(REP);
    set_zmq_const(XREQ);
    set_zmq_const(XREP);
    set_zmq_const(PULL);
    set_zmq_const(PUSH);

    set_zmq_const(HWM);
    set_zmq_const(SWAP);
    set_zmq_const(AFFINITY);
    set_zmq_const(IDENTITY);
    set_zmq_const(SUBSCRIBE);
    set_zmq_const(UNSUBSCRIBE);
    set_zmq_const(RATE);
    set_zmq_const(RECOVERY_IVL);
    set_zmq_const(MCAST_LOOP);
    set_zmq_const(SNDBUF);
    set_zmq_const(RCVBUF);
    set_zmq_const(RCVMORE);

    set_zmq_const(NOBLOCK);
    set_zmq_const(SNDMORE);

    return 1;
}
