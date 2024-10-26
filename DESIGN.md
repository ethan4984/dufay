# Scheduling

There exists a pre-processor scheduling server that utilises a circular queue within shared memory for object passing between the kernel and the user-space scheduler. The kernel manages brute context switching, while the user-space scheduler handles scheduling among various contexts with differing priorities. When the kernel depletes this queue, it shall reschedule to the pre-processor scheduling server which will then refill this queue and yield. The scheduler implements a Completely Fair paradigm, where each thread enqueued is the next left-most node within a Red-Black tree with respect to virtual runtime. 

![image](schedule.png)

To allow for efficent load-balancing and communication between schedulers, information and metadata are exchanged between servers via a protal link. Where the servers `processor_id` acts as an index into shared memory which points to its respective metadata structure. 

The notifications `NOTIFY_ENQUEUE_THREAD` and `NOTIFY_DEQUEUE_THREAD` allow for the enqueuing and dequeuing of threads within a scheduler server. When a `NOTIFY_ENQUEUE_THREAD` notification is invoked, it can trigger an additional notification to off-load the thread to another server to ensure balanced load between processors.

# Notifications

To send a burst of infomration that requires mutual processing between different contexts, this can be done with a notification. Each notification has an associated weight, which describes the priority in which it should be delivered. Each context has a possible 64 distinct notificaitons and it is understood that each party involved in the notification understands how the notification is defined.

- **NOTIFY_WEIGHT_SCHEDULED**: Delivered when it would have otherwise been scheduled to
- **NOTIFY_WEIGHT_TICK**: Delivered upon the next tick of the APIC-timer (which mediates the frequency of context-switches)
- **NOTIFY_WEIGHT_INSTANTANEOUS**: Delivered instantaneously in a single continuous chain of execution

We support nested notifications, allowing for the handling of a notification while within a notifiaction. Each context implements a queue of `ucontext` objects that represent various saved states within the same address space. Which enables each context to maintain a tree of different `ucontext` instances, allowing for traversal between different states across different levels within the chain of execution. 

Each notification works with respect to its own stack. But since the kernel has minimal authority of the address-space of a user-space server, for a server to support notifications it must pre-allocate a set of notification stacks. When nested notifications occour, the kernel will pull from this set and when exhausted it will invokve a notification `NOTIFY_USTACK_REFILL`. 

A notification handler will exist like this. Where NotificationInfo provides information about the sender and the nature of the call, the data parameter is a region allocated in shared memory for object passing. Notifications provide granular input over-shared memory, and granular output over-shared memory, intended such that a caller will supply the notification invocation with some set of objects, and the notification shall return its own set of objects.

```c
void notification(fayt::NotificationInfo *info, void *data, int not) {
  ...
  fayt::syscall(NOTIFICATION_RETURN);
}
```

# Memory Portals

```c
struct portal_req {
    int type;
    int prot;
    int length;

    struct __attribute__((packed)) {
        const char *identifier;
        int type;
        int create;
    } share;

    struct {
        const char *name;
        const char *message;
    } sp_req_chain;

    struct __attribute__((packed)) {
        uintptr_t addr;
        size_t length;
        uint64_t paddr[];
    } morphology;
} __attribute__((packed));
```

### Class

Often certain portal classifications are only accessible by special categories of server. A memory portal may belong to many such classifications and combinations of:

- **Share**: Establishes a region of shared memory between servers used for special types of protected IPC.
- **Direct**: Is used to create a direct map for a specified region in physical memory, mainly used for mapping MMIO.
- **Anon**: Creates an anonymous mapping of a specified size.
- **Cow**: A set of frames and references to be fulfilled in accordance with the principles of CoW (Copy on Write).
- **Special**: A special mapping for whose contents must be inferred from a specified chain of servers once written to or read from.

To resolve a page fault pertaining to a special page, the running thread must be blocked, and upon the response, the server is unblocked.

These are all special operations that require special permissions to invoke.

### Portal link over shared memory 

Shared memory for object passing can be very powerful when paired with the right protocols and interfaces to reduce complexity.

Certain applications require less safety and assurance than others. For example, a unidirectional queue from user-space to kernel-space requires minimal safe-guard. Another common application is shared metadata between multiple instances of the same server, requiring only locking. These basic applications are all that is required for the multi-server scheduling interface to function.

- In these scenarios, it is both the client’s and server’s responsibility to understand the nature of the data passed over shared memory. FAYT will provide macros and wrappers for data access to ensure locking.

- There will exist a table of shared memory portals, maintained in kernel space, each with an identifier, a list of the captured threads, and a pointer to the physical memory of the shared object. Each thread, in effect, manages its own virtual address space, so it will decide where it wants the share-point to be established.

- To create a share-point, you will call the portal system call, passing both the ANON (or DIRECT) and SHARE flags. You will provide a name identifying the share-point and pass a proper morphology. Then the caller will populate it with the share objects. Among any share-point, the first bytes will always be a meta-structure understood by all parties to be a governing object used for synchronization, defined as:

    ```c
    struct [[gnu::packed]] portal_link {
        char lock;
    
        int length;
        int header_offset;
        int header_limit;
        int data_offset;
        int data_limit;
    
        char data[];
    };
    ```

- All operations performed onto or adjacent to this link, will be required to be routed through a macro known as `OPERATE_LINK(LINK, CLASS, OPERATION)` where LINK is assumed to be a pointer to the portal, CLASS assumed to be the class of portal (LINK_CIRCULAR, LINK_VECTOR, LINK_RAW, ...), and OPERATION assumed to be a statement expression. Example shown below

    ```c
    int ret = OPERATE_LINK(link, LINK_CIRCULAR,
        ({
            struct thread *thread;
            int ret = traverse_and_queue(&thread);
            if(ret != -1) ret = circular_queue_push((void*)link->data, thread);
            ret;
        })
    );
    ```

- The share-point begins at the next 16-byte aligned address following this meta-structure. All access to the share-point will be understood to only be accessed by a set of wrappers that ensure all locking, protection, and boundary conditions are respected.

- Perhaps more advanced applications, such as bidirectional queues between servers, will require validation akin to that of Unix domain sockets. A rigorous connection between client and server must be established and maintained. For something like this, which would involve frequent blocking, a capability that can only be provided by an advanced set of schedulers. So, I will dive deeper into this design later.
