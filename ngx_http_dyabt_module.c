#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#define NGX_HTTP_DYABT_SHM_NODE_SIZE           (256)
#define NGX_HTTP_DYABT_SHM_CASES_SIZE          (64)
#define NGX_HTTP_DYABT_SHM_PARSER_SIZE         (64)
#define NGX_HTTP_DYABT_SHM_DOMAIN_SIZE         (128)

#define ngx_dyabt_add_timer(ev, timeout) \
    if (!ngx_exiting && !ngx_quit) ngx_add_timer(ev, (timeout))

typedef long long (*ngx_http_dyabt_parser_ptr_t)(ngx_http_request_t *r);

typedef struct {
    long long                      min;
    long long                      max;
}ngx_http_dyabt_case_t;

typedef struct {
    ngx_http_dyabt_parser_ptr_t    parser;
    ngx_array_t                   *cases;
}ngx_http_dyabt_testing_t;

typedef struct {
    ngx_http_dyabt_parser_ptr_t    parser;
    ngx_str_t                      name;
}ngx_http_dyabt_parser_t;

typedef struct ngx_http_dyabt_shm_node_s{
    u_char                            domain[NGX_HTTP_DYABT_SHM_DOMAIN_SIZE];
    u_char                            parser[NGX_HTTP_DYABT_SHM_PARSER_SIZE];
    ngx_http_dyabt_case_t             cases[NGX_HTTP_DYABT_SHM_CASES_SIZE];
    ngx_int_t                         domain_len;
    ngx_int_t                         parser_len;
    ngx_int_t                         cases_len;
    struct ngx_http_dyabt_shm_node_s *next;
}ngx_http_dyabt_shm_node_t;

typedef struct {
    ngx_shmtx_sh_t                 lock;
    ngx_int_t                      version;
    ngx_http_dyabt_shm_node_t     *enable;
    ngx_http_dyabt_shm_node_t     *disable;
}ngx_http_dyabt_shm_t;

typedef struct {
    ngx_event_t                    timer;
    ngx_hash_t                    *hash;
    ngx_pool_t                    *pool;
    ngx_array_t                   *domains;
    ngx_hash_t                    *parsers;
    ngx_pool_t                    *global_pool;
    ngx_http_dyabt_shm_t          *shm;
    ngx_shmtx_t                    lock;
    ngx_int_t                      version;
}ngx_http_dyabt_global_ctx_t;

typedef struct {
    ngx_str_t                     *domain;
    ngx_array_t                   *lengths;
    ngx_array_t                   *values;
    ngx_int_t                      values_count;
}ngx_http_dyabt_set_conf_t;

typedef struct {
    ngx_flag_t                     enable;
} ngx_http_dyabt_main_conf_t;

static ngx_int_t
ngx_http_dyabt_init_process(ngx_cycle_t *cycle);

static void
ngx_http_dyabt_on_timer(ngx_event_t *ev);

static char *
ngx_http_dyabt_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static ngx_int_t
ngx_http_dyabt_set_handler(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data);

static char *
ngx_http_dyabt_value(ngx_conf_t *cf, ngx_http_dyabt_set_conf_t *scf, ngx_str_t *value);

long long
ngx_http_dyabt_uid_parser(ngx_http_request_t *r);

ngx_table_elt_t *
ngx_http_dyabt_search_headers(ngx_http_request_t *r, u_char *name, size_t len);

static char *
ngx_http_dyabt_interface(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static char *
ngx_http_dyabt_init_main_conf(ngx_conf_t *cf, void *conf);

static void *
ngx_http_dyabt_create_main_conf(ngx_conf_t *cf);

static ngx_int_t
ngx_http_dyabt_do_finish(ngx_http_request_t *r, ngx_int_t status, ngx_str_t *rv);

static ngx_int_t
ngx_http_dyabt_interface_handler(ngx_http_request_t *r);

static ngx_buf_t *
ngx_http_dyabt_read_body_from_file(ngx_http_request_t *r);

static ngx_buf_t *
ngx_http_dyabt_read_body(ngx_http_request_t *r);

static void
ngx_http_dyabt_body_handler(ngx_http_request_t *r);

static ngx_int_t
ngx_http_dyabt_do_post(ngx_http_request_t *r);

static void
ngx_http_dyabt_parser_testing(ngx_http_request_t *r , ngx_buf_t *body);

static ngx_int_t
ngx_http_dyabt_make_conf(ngx_log_t *log);

static ngx_int_t
ngx_http_dyabt_do_delete(ngx_http_request_t *r);

static ngx_int_t
ngx_http_dyabt_do_get(ngx_http_request_t *r);

ngx_int_t
ngx_http_dyabt_search_ip(ngx_http_request_t *r, ngx_str_t *ip);

long long
ngx_http_dyabt_ip_parser(ngx_http_request_t *r);

long long
ngx_http_dyabt_inet_aton(u_char *data, ngx_uint_t len);

long long
ngx_http_dyabt_atoll(u_char* data,ngx_int_t len);

static ngx_http_dyabt_global_ctx_t ngx_http_dyabt_global_ctx;

static ngx_http_module_t  ngx_http_dyabt_module_ctx = {
    NULL,                             /* preconfiguration */
    NULL,                             /* postconfiguration */

    ngx_http_dyabt_create_main_conf,  /* create main configuration */
    ngx_http_dyabt_init_main_conf,    /* init main configuration */

    NULL,                             /* create server configuration */
    NULL,                             /* merge server configuration */

    NULL,                             /* create location configuration */
    NULL                              /* merge location configuration */
};

static ngx_command_t  ngx_http_dyabt_commands[] = {
    {
        ngx_string("dyabt_set"),
        NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
        ngx_http_dyabt_set,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
    {
        ngx_string("dyabt_interface"),
        NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
        ngx_http_dyabt_interface,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
    ngx_null_command
};

ngx_module_t  ngx_http_dyabt_module = {
    NGX_MODULE_V1,
    &ngx_http_dyabt_module_ctx,    /* module context */
    ngx_http_dyabt_commands,       /* module directives */
    NGX_HTTP_MODULE,               /* module type */
    NULL,                          /* init master */
    NULL,                          /* init module */
    ngx_http_dyabt_init_process,   /* init process */
    NULL,                          /* init thread */
    NULL,                          /* exit thread */
    NULL,                          /* exit process */
    NULL,                          /* exit master */
    NGX_MODULE_V1_PADDING
};

static char *
ngx_http_dyabt_interface(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t    *clcf;
    ngx_http_dyabt_main_conf_t  *dmcf;

    dmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_dyabt_module);
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_dyabt_interface_handler;
    dmcf->enable = 1;

    return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_dyabt_interface_handler(ngx_http_request_t *r)
{
    ngx_int_t             status = NGX_HTTP_NOT_FOUND;
    ngx_str_t             rv = ngx_string("not found");
    if(r->uri.len >= 9 && ngx_strncasecmp(r->uri.data, (u_char*)"/testings",9) == 0){
        switch (r->method) {
            case NGX_HTTP_GET:
                if(r->uri.len >= 9){
                    return ngx_http_dyabt_do_get(r);
                }
                break;
            case NGX_HTTP_POST:
                if(r->uri.len == 9){
                    return ngx_http_dyabt_do_post(r);
                }
                break;
            case NGX_HTTP_DELETE:
                return ngx_http_dyabt_do_delete(r);
                break;
        }
    }
    return ngx_http_dyabt_do_finish(r, status, &rv);
}

static ngx_int_t
ngx_http_dyabt_do_finish(ngx_http_request_t *r, ngx_int_t status, ngx_str_t *rv)
{
    ngx_int_t             rc;
    ngx_buf_t            *b;
    ngx_chain_t           out;
    r->headers_out.status = status;
    r->headers_out.content_length_n = rv->len;

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK) {
        return rc;
    }

    if (rv->len == 0) {
        return ngx_http_send_special(r, NGX_HTTP_FLUSH);
    }

    b = ngx_create_temp_buf(r->pool, rv->len);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->pos = rv->data;
    b->last = rv->data + rv->len;
    b->last_buf = 1;

    out.buf = b;
    out.next = NULL;
    ngx_http_finalize_request(r, ngx_http_output_filter(r, &out));
    return rc;
}

static ngx_int_t
ngx_http_dyabt_do_post(ngx_http_request_t *r)
{
    ngx_int_t  rc;
    ngx_log_error(NGX_LOG_ERR,r->connection->log,0,"ngx_http_dyabt_do_post");

    rc = ngx_http_read_client_request_body(r, ngx_http_dyabt_body_handler);

    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }
    return NGX_DONE;
}

static ngx_int_t
ngx_http_dyabt_do_delete(ngx_http_request_t *r)
{
    ngx_int_t                  status = NGX_HTTP_OK;
    ngx_str_t                  result = ngx_string("success.");;
    ngx_str_t                  domain;
    ngx_http_dyabt_shm_node_t *p,*prev,*target;
    if(r->uri.len == 9){
        status = NGX_HTTP_NOT_FOUND;
        ngx_str_set(&result,"not found domain.");
        ngx_http_dyabt_do_finish(r,status,&result);
        return NGX_ERROR;
    }
    if(r->uri.data[9] != '/'){
        status = NGX_HTTP_BAD_REQUEST;
        ngx_str_set(&result,"format error.");
        ngx_http_dyabt_do_finish(r,status,&result);
        return NGX_ERROR;
    }
    // substring after /testings
    domain.data = r->uri.data + 10;
    domain.len = r->uri.len - 10;
    if(ngx_http_dyabt_global_ctx.shm == NULL){
        status = NGX_HTTP_SERVICE_UNAVAILABLE;
        ngx_str_set(&result,"sherad memory not initialized.");
        ngx_http_dyabt_do_finish(r,status,&result);
        return NGX_ERROR;
    }
    if(!ngx_shmtx_trylock(&ngx_http_dyabt_global_ctx.lock)){
        status = NGX_HTTP_SERVICE_UNAVAILABLE;
        ngx_str_set(&result,"sherad memory lock can't locked.");
        ngx_http_dyabt_do_finish(r,status,&result);
        return NGX_ERROR;
    }
    p = ngx_http_dyabt_global_ctx.shm->enable;
    target = NULL;
    prev = NULL;
    while(p){
        if(p->domain_len==(ngx_int_t)domain.len && ngx_strncmp(p->domain,domain.data,domain.len)==0){
            target = p;
            break;
        }
        prev = p;
        p = p->next;
    }
    if(target){
        if(prev){
            prev->next = target->next;
        }else{
            ngx_http_dyabt_global_ctx.shm->enable = target->next;
        }
        target->next = ngx_http_dyabt_global_ctx.shm->disable;
        ngx_http_dyabt_global_ctx.shm->disable = target;
        ngx_http_dyabt_global_ctx.shm->version++;
    }else{
        status = NGX_HTTP_NOT_FOUND;
        ngx_str_set(&result,"not found.");
    }
    ngx_shmtx_unlock(&ngx_http_dyabt_global_ctx.lock);
    ngx_http_dyabt_do_finish(r,status,&result);
    return NGX_OK;
}

static ngx_int_t
ngx_http_dyabt_do_get(ngx_http_request_t *r)
{
    u_char                    *p,*last;
    ngx_int_t                  status,case_index,size;
    ngx_str_t                  result,domain,parser,target;
    ngx_http_dyabt_shm_t      *shm;
    ngx_http_dyabt_shm_node_t *node;
    if(ngx_http_dyabt_global_ctx.shm == NULL){
        status = NGX_HTTP_SERVICE_UNAVAILABLE;
        ngx_str_set(&result,"sherad memory not initialized.");
        ngx_http_dyabt_do_finish(r,status,&result);
        return NGX_ERROR;
    }
    target.len = 0;
    if( r->uri.len > 9 ){
        if(r->uri.data[9] == '/'){
            target.data = r->uri.data + 10;
            target.len = r->uri.len - 10;
        }else{
            status = NGX_HTTP_BAD_REQUEST;
            ngx_str_set(&result,"format error.");
            ngx_http_dyabt_do_finish(r,status,&result);
            return NGX_ERROR;
        }
    }
    if(!ngx_shmtx_trylock(&ngx_http_dyabt_global_ctx.lock)){
        status = NGX_HTTP_SERVICE_UNAVAILABLE;
        ngx_str_set(&result,"sherad memory lock can't locked.");
        ngx_http_dyabt_do_finish(r,status,&result);
        return NGX_ERROR;
    }
    shm = ngx_http_dyabt_global_ctx.shm;
    size = NGX_HTTP_DYABT_SHM_NODE_SIZE*sizeof(ngx_http_dyabt_shm_node_t);
    result.len = 0;
    result.data = ngx_pnalloc(r->pool,size);
    p = result.data;
    last = p + size;
    node = shm->enable;
    while(node!=NULL){
        domain.len = node->domain_len;
        domain.data = node->domain;
        parser.len = node->parser_len;
        parser.data = node->parser;
        if(target.len == 0 || (domain.len == target.len && ngx_strncmp(domain.data,target.data,domain.len) == 0)){
            p = ngx_snprintf(p,last-p,"%V,%V\n",&domain,&parser);
            for(case_index=0;case_index<node->cases_len;++case_index){
                p = ngx_snprintf(p,last-p,"%d,%d\n",node->cases[case_index].min,node->cases[case_index].max);
            }
            if(target.len != 0){
                break;
            }
            p = ngx_snprintf(p,last-p,"---\n");
        }
        node = node->next;
    }
    result.len = p - result.data;
    ngx_shmtx_unlock(&ngx_http_dyabt_global_ctx.lock);
    status = NGX_HTTP_OK;
    ngx_http_dyabt_do_finish(r,status,&result);
    return NGX_OK;
}

static void
ngx_http_dyabt_body_handler(ngx_http_request_t *r)
{
    ngx_buf_t               *body;
    if (r->request_body->temp_file) {
        body = ngx_http_dyabt_read_body_from_file(r);
    } else {
        body = ngx_http_dyabt_read_body(r);
    }
    ngx_http_dyabt_parser_testing(r,body);
}

static ngx_int_t
ngx_http_dyabt_parser_line(u_char **p,u_char *last,
    ngx_str_t *one, ngx_str_t *two){
    u_char            *start;
    ngx_int_t          count;
    ngx_int_t          len;
    start = *p;
    count = 0;
    len = 0;
    while(*p <= last){
        if(**p == '\r' || **p == '\n' || **p == ','){
            if(len == 0){
                ++start;
            }else{
                if(count == 0){
                    one->len = len;
                    one->data = start;
                }else if(count == 1){
                    two->len = len;
                    two->data = start;
                }
                len = 0;
                start = *p + 1;
                ++count;
            }
            if(**p=='\n'){
                ++*p;
                break;
            }
        }else{
            ++len;
        }
        ++*p;
    }
    return count;
}

static void
ngx_http_dyabt_parser_testing(ngx_http_request_t *r , ngx_buf_t *body)
{
    ngx_str_t                  domain,parser,response,min_str,max_str;
    ngx_int_t                  status;
    long long                  min,max;
    u_char                    *p;
    ngx_http_dyabt_shm_node_t  node;
    ngx_http_dyabt_shm_node_t *temp,*target;
    p = body->pos;
    ngx_memzero(&domain,sizeof(ngx_str_t));
    ngx_memzero(&parser,sizeof(ngx_str_t));
    status = ngx_http_dyabt_parser_line(&p,body->last,&domain,&parser);
    if(status != 2){
         ngx_str_set(&response,"domain or parser not found.");
         ngx_http_dyabt_do_finish(r,NGX_HTTP_BAD_REQUEST,&response);
         return;
    }
    if(ngx_hash_find(ngx_http_dyabt_global_ctx.parsers,
            ngx_hash_key(parser.data,parser.len),
            parser.data,parser.len) == NULL) {
        ngx_str_set(&response,"parser not supported.");
        ngx_http_dyabt_do_finish(r,NGX_HTTP_BAD_REQUEST,&response);
        return;
    }
    node.domain_len = domain.len;
    ngx_memcpy(node.domain,domain.data,domain.len);
    node.parser_len = parser.len;
    ngx_memcpy(node.parser,parser.data,parser.len);
    node.cases_len = 0;
    while(ngx_http_dyabt_parser_line(&p,body->last,&min_str,&max_str)==2){
        min = ngx_http_dyabt_atoll(min_str.data,min_str.len);
        max = ngx_http_dyabt_atoll(max_str.data,max_str.len);
        if(min==NGX_ERROR || max == NGX_ERROR){
            ngx_str_set(&response,"min or max is invalid number.");
            ngx_http_dyabt_do_finish(r,NGX_HTTP_BAD_REQUEST,&response);
            return;
        }
        node.cases[node.cases_len].min = min;
        node.cases[node.cases_len].max = max;
        node.cases_len++;
    }
    if(ngx_http_dyabt_global_ctx.shm != NULL){
        if(ngx_shmtx_trylock(&ngx_http_dyabt_global_ctx.lock)){
            temp = ngx_http_dyabt_global_ctx.shm->enable;
            target = NULL;
            while(temp){
                if(temp->domain_len == node.domain_len && ngx_strncmp(temp->domain,node.domain,node.domain_len) == 0){
                    target = temp;
                    break;
                }
                temp = temp->next;
            }
            if(target != NULL){
                node.next = target->next;
                *target = node;
                status = NGX_HTTP_OK;
                ngx_str_set(&response,"success.");
            }else{
                if(ngx_http_dyabt_global_ctx.shm->disable != NULL){
                    target = ngx_http_dyabt_global_ctx.shm->disable;
                    ngx_http_dyabt_global_ctx.shm->disable = target->next;
                    *target = node;
                    target->next = ngx_http_dyabt_global_ctx.shm->enable;
                    ngx_http_dyabt_global_ctx.shm->enable = target;
                    ngx_http_dyabt_global_ctx.shm->version++;
                    status = NGX_HTTP_OK;
                    ngx_str_set(&response,"success.");
                }else{
                    status = NGX_HTTP_SERVICE_UNAVAILABLE;
                    ngx_str_set(&response,"shared memory node full.");
                }
            }
            ngx_shmtx_unlock(&ngx_http_dyabt_global_ctx.lock);
        }else{
            status = NGX_HTTP_SERVICE_UNAVAILABLE;
            ngx_str_set(&response,"shared memory lock can't locked.");
        }
    }else{
        status = NGX_HTTP_SERVICE_UNAVAILABLE;
        ngx_str_set(&response,"shared memory not initialized.");
    }
    ngx_http_dyabt_do_finish(r,status,&response);
}

static ngx_buf_t *
ngx_http_dyabt_read_body(ngx_http_request_t *r)
{
    size_t        len;
    ngx_buf_t    *buf, *next, *body;
    ngx_chain_t  *cl;
    cl = r->request_body->bufs;
    buf = cl->buf;
    if (cl->next == NULL) {
        return buf;
    } else {
        next = cl->next->buf;
        len = (buf->last - buf->pos) + (next->last - next->pos);
        body = ngx_create_temp_buf(r->pool, len);
        if (body == NULL) {
            return NULL;
        }
        body->last = ngx_cpymem(body->last, buf->pos, buf->last - buf->pos);
        body->last = ngx_cpymem(body->last, next->pos, next->last - next->pos);
    }
    return body;
}


static ngx_buf_t *
ngx_http_dyabt_read_body_from_file(ngx_http_request_t *r)
{
    size_t        len;
    ssize_t       size;
    ngx_buf_t    *buf, *body;
    ngx_chain_t  *cl;
    len = 0;
    cl = r->request_body->bufs;
    while (cl) {
        buf = cl->buf;
        if (buf->in_file) {
            len += buf->file_last - buf->file_pos;
        } else {
            len += buf->last - buf->pos;
        }
        cl = cl->next;
    }
    body = ngx_create_temp_buf(r->pool, len);
    if (body == NULL) {
        return NULL;
    }
    cl = r->request_body->bufs;
    while (cl) {
        buf = cl->buf;
        if (buf->in_file) {
            size = ngx_read_file(buf->file, body->last,
                                 buf->file_last - buf->file_pos, buf->file_pos);
            if (size == NGX_ERROR) {
                return NULL;
            }
            body->last += size;
        } else {
            body->last = ngx_cpymem(body->last, buf->pos, buf->last - buf->pos);
        }
        cl = cl->next;
    }
    return body;
}


static char *
ngx_http_dyabt_init_main_conf(ngx_conf_t *cf, void *conf)
{
    ngx_int_t                    node_index;
    ngx_shm_t                    shm;
    ngx_http_dyabt_main_conf_t  *dmcf = conf;
    ngx_http_dyabt_shm_node_t   *p;
    ngx_memzero(&ngx_http_dyabt_global_ctx,sizeof(ngx_http_dyabt_global_ctx_t));
    if(dmcf->enable == NGX_CONF_UNSET){
        dmcf->enable = 0;
    }
    if(!dmcf->enable){
        return NGX_CONF_OK;
    }
    /* initializ shared memory*/
    ngx_memzero(&shm,sizeof(ngx_shm_t));
    ngx_str_set(&shm.name,"ngx_http_dyabt_shm");
    shm.size = sizeof(ngx_http_dyabt_shm_t)+sizeof(ngx_http_dyabt_shm_node_t)*NGX_HTTP_DYABT_SHM_NODE_SIZE;
    shm.log = cf->log;
    if(ngx_shm_alloc(&shm)!=NGX_OK){
        return NGX_CONF_ERROR;
    }
    ngx_log_error(NGX_LOG_INFO,cf->log,0,"shared memory alloc success.");
    ngx_http_dyabt_global_ctx.shm = (ngx_http_dyabt_shm_t *)shm.addr;

    /* initializ node link */
    p = (ngx_http_dyabt_shm_node_t*)(shm.addr + sizeof(ngx_http_dyabt_shm_t));
    ngx_http_dyabt_global_ctx.shm->disable = p;
    ngx_http_dyabt_global_ctx.shm->enable = NULL;
    for(node_index=0;node_index<NGX_HTTP_DYABT_SHM_NODE_SIZE;++node_index){
        p->next = p+1;
        ++p;
    }
    (p-1)->next = NULL;
    /* initializ lock */
    ngx_http_dyabt_global_ctx.shm->lock.lock = 0;
    if(ngx_shmtx_create(&ngx_http_dyabt_global_ctx.lock,
        &ngx_http_dyabt_global_ctx.shm->lock,
        (u_char*)"ngx_http_dyabt_shm_lock") != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    /* initializ version */
    ngx_http_dyabt_global_ctx.shm->version = 0;
    ngx_http_dyabt_global_ctx.version = -1;

    return NGX_CONF_OK;
}

static void *
ngx_http_dyabt_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_dyabt_main_conf_t  *dmcf;
    dmcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_dyabt_main_conf_t));
    if (dmcf == NULL) {
        return NULL;
    }
    dmcf->enable = NGX_CONF_UNSET;
    return dmcf;
}

static char *
ngx_http_dyabt_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t                           *value;
    ngx_http_variable_t                 *v;
    ngx_http_dyabt_set_conf_t           *scf;
    value = cf->args->elts;
    if (value[1].data[0] != '$') {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid variable name \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }
    value[1].len--;
    value[1].data++;
    v = ngx_http_add_variable(cf, &value[1], NGX_HTTP_VAR_CHANGEABLE);
    if (v == NULL) {
        return NGX_CONF_ERROR;
    }

    scf = ngx_pnalloc(cf->pool, sizeof(ngx_http_dyabt_set_conf_t));
    scf->domain = ngx_pnalloc(cf->pool, sizeof(ngx_str_t));
    scf->domain->len = value[2].len;
    scf->domain->data = ngx_pnalloc(cf->pool, value[2].len);
    ngx_memcpy(scf->domain->data,value[2].data,scf->domain->len);
    if (ngx_http_dyabt_value(cf, scf, &value[2]) != NGX_CONF_OK) {
        return NGX_CONF_ERROR;
    }
    v->get_handler = ngx_http_dyabt_set_handler;
    v->data = (uintptr_t)scf;
    return NGX_CONF_OK;
}

static char *
ngx_http_dyabt_value(ngx_conf_t *cf, ngx_http_dyabt_set_conf_t *scf, ngx_str_t *value)
{
    ngx_int_t                              n;
    ngx_http_script_compile_t              sc;
    n = ngx_http_script_variables_count(value);
    if (n) {
        ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));
        sc.cf = cf;
        sc.source = value;
        sc.lengths = &scf->lengths;
        sc.values = &scf->values;
        sc.variables = n;
        sc.complete_lengths = 1;
        if (ngx_http_script_compile(&sc) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }
    scf->values_count = n;
    return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_dyabt_set_handler(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_int_t                            result;
    ngx_str_t                            value;
    ngx_http_dyabt_set_conf_t           *scf;
    ngx_http_dyabt_testing_t            *testing;
    ngx_http_dyabt_case_t               *cases;
    ngx_int_t                            size;
    ngx_int_t                            testing_result = -1;
    ngx_http_dyabt_main_conf_t          *dmcf;
    dmcf = ngx_http_get_module_main_conf(r, ngx_http_dyabt_module);
    if(!dmcf->enable){
        v->data = (u_char*)"0";
        v->len  = 1;
        return NGX_OK;
    }
    scf = (ngx_http_dyabt_set_conf_t*)data;
    if(scf->values_count){
        if (ngx_http_script_run(r, &value, scf->lengths->elts, 0, scf->values->elts) == NULL)
        {
            return NGX_ERROR;
        }
    }else{
        value = *(scf->domain);
    }
    result = 0;
    testing = ngx_hash_find(ngx_http_dyabt_global_ctx.hash,
        ngx_hash_key(value.data,value.len),
        value.data,value.len);
    if(testing){
        cases = testing->cases->elts;
        size = testing->cases->nelts;
        testing_result = testing->parser(r);
        for(result=0;result<size;result++){
            if(cases[result].min<=testing_result && cases[result].max>=testing_result){
                break;
            }
        }
        if(result>=size){
            result = -1;
        }
        //begin form 1 ,default is 0.
        result++;
    }
    v->data = ngx_pnalloc(r->pool, 3);
    v->len = ngx_snprintf(v->data,3,"%d",result) - v->data;
    return NGX_OK;
}

static ngx_int_t ngx_http_dyabt_make_conf(ngx_log_t *log)
{
    ngx_pool_t                 *temp_pool;
    ngx_hash_init_t             hash_init;
    ngx_int_t                   result;
    ngx_int_t                   cese_index;
    ngx_hash_key_t             *domain_hash;
    ngx_http_dyabt_case_t      *testing_case;
    ngx_http_dyabt_testing_t   *testing;
    ngx_http_dyabt_shm_node_t  *node;
    if(ngx_shmtx_trylock(&ngx_http_dyabt_global_ctx.lock)){
        ngx_log_error(NGX_LOG_ERR, log, 0, "[dyabt] make conf %d->%d",ngx_http_dyabt_global_ctx.version,ngx_http_dyabt_global_ctx.shm->version);
        if(ngx_http_dyabt_global_ctx.shm->version > ngx_http_dyabt_global_ctx.version){
            if(ngx_http_dyabt_global_ctx.pool != NULL){
                ngx_destroy_pool(ngx_http_dyabt_global_ctx.pool);
            }
            ngx_http_dyabt_global_ctx.pool = ngx_create_pool(
                (NGX_DEFAULT_POOL_SIZE*1024),log);
            if(ngx_http_dyabt_global_ctx.pool == NULL){
                ngx_log_error(NGX_LOG_ERR, log, 0, "[dyabt] pool create error");
                ngx_shmtx_unlock(&ngx_http_dyabt_global_ctx.lock);
                return NGX_ERROR;
            }
            temp_pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE,log);
            if(temp_pool == NULL){
                ngx_log_error(NGX_LOG_ERR, log, 0, "[dyabt] pool create error");
                ngx_shmtx_unlock(&ngx_http_dyabt_global_ctx.lock);
                return NGX_ERROR;
            }
            ngx_http_dyabt_global_ctx.domains = ngx_pnalloc(
                ngx_http_dyabt_global_ctx.pool,
                sizeof(ngx_array_t));
            if(ngx_http_dyabt_global_ctx.domains == NULL){
                ngx_log_error(NGX_LOG_ERR, log, 0, "[dyabt] domains alloc error");
                ngx_shmtx_unlock(&ngx_http_dyabt_global_ctx.lock);
                return NGX_ERROR;
            }
            ngx_memzero(ngx_http_dyabt_global_ctx.domains,sizeof(ngx_array_t));
            result = ngx_array_init(ngx_http_dyabt_global_ctx.domains,
                ngx_http_dyabt_global_ctx.pool,
                16, sizeof(ngx_hash_key_t));
            if(result == NGX_ERROR){
                ngx_log_error(NGX_LOG_ERR, log, 0, "[dyabt] domains array init error");
                ngx_shmtx_unlock(&ngx_http_dyabt_global_ctx.lock);
                return NGX_ERROR;
            }
            if(ngx_http_dyabt_global_ctx.shm != NULL){
                node = ngx_http_dyabt_global_ctx.shm->enable;
                while(node!=NULL){
                    testing = ngx_pnalloc(
                        ngx_http_dyabt_global_ctx.pool,
                        sizeof(ngx_http_dyabt_testing_t));
                    if(testing == NULL){
                        ngx_log_error(NGX_LOG_ERR, log, 0, "[dyabt] testing alloc error");
                        ngx_shmtx_unlock(&ngx_http_dyabt_global_ctx.lock);
                        return NGX_ERROR;
                    }
                    testing->parser = ngx_hash_find(ngx_http_dyabt_global_ctx.parsers,
                            ngx_hash_key(node->parser,node->parser_len),
                            node->parser,node->parser_len);
                    if(testing->parser == NULL){
                        ngx_log_error(NGX_LOG_ERR, log, 0, "[dyabt] parser not supper error");
                        node = node->next;
                        continue;
                    }
                    testing->cases = ngx_pnalloc(
                        ngx_http_dyabt_global_ctx.pool,
                        sizeof(ngx_array_t));
                    if(testing->cases == NULL){
                        ngx_log_error(NGX_LOG_ERR, log, 0, "[dyabt] testing cases alloc error");
                        ngx_shmtx_unlock(&ngx_http_dyabt_global_ctx.lock);
                        return NGX_ERROR;
                    }
                    result = ngx_array_init(testing->cases,
                        ngx_http_dyabt_global_ctx.pool,
                        4, sizeof(ngx_http_dyabt_case_t));
                    if(result == NGX_ERROR){
                        ngx_log_error(NGX_LOG_ERR, log, 0, "[dyabt] testing cases array init error");
                        ngx_shmtx_unlock(&ngx_http_dyabt_global_ctx.lock);
                        return NGX_ERROR;
                    }
                    for(cese_index=0;cese_index<node->cases_len;++cese_index){
                        testing_case = ngx_array_push(testing->cases);
                        if(testing_case == NULL){
                            ngx_log_error(NGX_LOG_ERR, log, 0, "[dyabt] testing cases array push error");
                            ngx_shmtx_unlock(&ngx_http_dyabt_global_ctx.lock);
                            return NGX_ERROR;
                        }
                        *testing_case = node->cases[cese_index];
                    }
                    domain_hash = ngx_array_push(ngx_http_dyabt_global_ctx.domains);
                    if(domain_hash == NULL){
                        ngx_log_error(NGX_LOG_ERR, log, 0, "[dyabt] domains array push error");
                        ngx_shmtx_unlock(&ngx_http_dyabt_global_ctx.lock);
                        return NGX_ERROR;
                    }
                    domain_hash->key.len = node->domain_len;
                    domain_hash->key.data = ngx_pnalloc(ngx_http_dyabt_global_ctx.pool,node->domain_len);
                    if(domain_hash->key.data == NULL){
                        ngx_log_error(NGX_LOG_ERR, log, 0, "[dyabt] domain hash key alloc error");
                        ngx_shmtx_unlock(&ngx_http_dyabt_global_ctx.lock);
                        return NGX_ERROR;
                    }
                    ngx_memcpy(domain_hash->key.data,node->domain,node->domain_len);
                    domain_hash->key_hash = ngx_hash_key(domain_hash->key.data,domain_hash->key.len);
                    domain_hash->value = testing;
                    // next node
                    node = node->next;
                }
            }
            ngx_http_dyabt_global_ctx.version = ngx_http_dyabt_global_ctx.shm->version;
            ngx_memzero(&hash_init,sizeof(ngx_hash_init_t));
            hash_init.key = ngx_hash_key;
            hash_init.max_size = 512;
            hash_init.bucket_size = ngx_align(64, ngx_cacheline_size);
            hash_init.name = "ngx_http_dyabt_domains_hash_table";
            hash_init.pool = ngx_http_dyabt_global_ctx.pool;
            hash_init.temp_pool = temp_pool;
            result = ngx_hash_init(&hash_init,
                ngx_http_dyabt_global_ctx.domains->elts,
                ngx_http_dyabt_global_ctx.domains->nelts
            );
            if(result == NGX_ERROR){
                ngx_log_error(NGX_LOG_ERR, log, 0, "[dyabt] hash init error");
                ngx_shmtx_unlock(&ngx_http_dyabt_global_ctx.lock);
                return NGX_ERROR;
            }
            ngx_http_dyabt_global_ctx.hash = hash_init.hash;
            ngx_destroy_pool(temp_pool);
        }
        ngx_shmtx_unlock(&ngx_http_dyabt_global_ctx.lock);
    }
    return NGX_OK;
}

static ngx_int_t ngx_http_dyabt_init_process(ngx_cycle_t *cycle)
{
    ngx_event_t                 *timer;
    ngx_int_t                    result;
    ngx_pool_t                  *temp_pool;
    ngx_hash_key_t              *parser;
    ngx_array_t                  parsers;
    ngx_hash_init_t              parser_hash_init;
    ngx_http_dyabt_main_conf_t  *dmcf;
    dmcf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_dyabt_module);
    if(!dmcf->enable){
        return NGX_OK;
    }
    ngx_http_dyabt_global_ctx.global_pool = ngx_create_pool(
        (NGX_DEFAULT_POOL_SIZE*1024),cycle->log);
    ngx_memzero(&parsers,sizeof(ngx_array_t));
    result = ngx_array_init(&parsers,
        ngx_http_dyabt_global_ctx.global_pool,
        4, sizeof(ngx_hash_key_t));
    if(result == NGX_ERROR){
        return NGX_ERROR;
    }
    // add parser function
    parser = ngx_array_push(&parsers);
    ngx_str_set(&parser->key,"header_x_uid");
    parser->key_hash = ngx_hash_key(parser->key.data,parser->key.len);
    parser->value = ngx_http_dyabt_uid_parser;

    parser = ngx_array_push(&parsers);
    ngx_str_set(&parser->key,"remote_ip");
    parser->key_hash = ngx_hash_key(parser->key.data,parser->key.len);
    parser->value = ngx_http_dyabt_ip_parser;
    // add parser hash map
    temp_pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE, cycle->log);
    ngx_memzero(&parser_hash_init,sizeof(ngx_hash_init_t));
    parser_hash_init.key = ngx_hash_key;
    parser_hash_init.max_size = 512;
    parser_hash_init.bucket_size = ngx_align(64, ngx_cacheline_size);
    parser_hash_init.name = "ngx_http_dyabt_parser_hash_table";
    parser_hash_init.pool = ngx_http_dyabt_global_ctx.global_pool;
    parser_hash_init.temp_pool = temp_pool;
    result = ngx_hash_init(&parser_hash_init,
        parsers.elts,
        parsers.nelts
    );
    if(result == NGX_ERROR){
        return NGX_ERROR;
    }
    ngx_http_dyabt_global_ctx.parsers = parser_hash_init.hash;
    // add parser end
    ngx_destroy_pool(temp_pool);
    // add timer
    timer = &ngx_http_dyabt_global_ctx.timer;
    ngx_memzero(timer,sizeof(ngx_event_t));
    timer->handler = ngx_http_dyabt_on_timer;
    timer->log = cycle->log;
    ngx_add_timer(timer,ngx_pid%10 * 100);
    return NGX_OK;
}

static void ngx_http_dyabt_on_timer(ngx_event_t *ev)
{
    ngx_log_error(NGX_LOG_ERR, ev->log, 0, "ngx_http_dyabt_on_timer:%d",ngx_http_dyabt_global_ctx.version);
    ngx_http_dyabt_make_conf(ev->log);
    ngx_dyabt_add_timer(ev, 3000);
}

ngx_table_elt_t *
ngx_http_dyabt_search_headers(ngx_http_request_t *r, u_char *name, size_t len) {
    ngx_list_part_t            *part;
    ngx_table_elt_t            *h;
    ngx_uint_t                  i;
    part = &r->headers_in.headers.part;
    h = part->elts;
    for (i = 0; /* void */ ; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            h = part->elts;
            i = 0;
        }
        if (len != h[i].key.len || ngx_strcasecmp(name, h[i].key.data) != 0) {
            continue;
        }
        return &h[i];
    }
    return NULL;
}



long long
ngx_http_dyabt_uid_parser(ngx_http_request_t *r){
    ngx_table_elt_t *h;
    h = ngx_http_dyabt_search_headers(r,(u_char*)"X-UID",5);
    if(h==NULL){
        return -1;
    }
    return ngx_http_dyabt_atoll(h->value.data,h->value.len);
}

ngx_int_t
ngx_http_dyabt_search_ip(ngx_http_request_t *r, ngx_str_t *ip)
{
    ngx_uint_t         n;
    ngx_table_elt_t  **h;
    n = r->headers_in.x_forwarded_for.nelts;
    h = r->headers_in.x_forwarded_for.elts;
    if(n > 0){
        *ip = h[0]->value;
        return NGX_OK;
    }else{
        *ip = r->connection->addr_text;
        return NGX_OK;
    }
    return NGX_ERROR;
}

long long
ngx_http_dyabt_inet_aton(u_char *data, ngx_uint_t len)
{
    long long  result = 0;
    long long  temp = 0;
    u_char      *last = data + len;
    int        point = 0;
    while(data<=last){
        if(*data == '.' || data == last){
            if(temp>255){
                return NGX_ERROR;
            }
            result = (result<<8) + temp;
            temp = 0;
            ++point;
            if(point == 4){
                break;
            }
        }else if(*data <= '9' && *data >= '0'){
            temp = temp * 10 + (*data - '0');
        }else {
            return NGX_ERROR;
        }
        ++data;
    }
    if(point != 4){
        return NGX_ERROR;
    }
    return result;
}

long long
ngx_http_dyabt_ip_parser(ngx_http_request_t *r){
    ngx_int_t result;
    ngx_str_t ip;
    result = ngx_http_dyabt_search_ip(r,&ip);
    if(result == NGX_ERROR){
        return NGX_ERROR;
    }
    result = ngx_http_dyabt_inet_aton(ip.data,ip.len);
    if(result == NGX_ERROR){
        return NGX_ERROR;
    }
    return result;
}

long long
ngx_http_dyabt_atoll(u_char* data,ngx_int_t len)
{
    long long         result = 0;
    u_char           *last = data+len;
    while(data<last){
        if(*data >= '0' && *data <= '9'){
            result *= 10;
            result += *data - '0';
        }else{
            return NGX_ERROR;
        }
        ++data;
    }
    return result;
}
