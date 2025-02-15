#ifndef NVME_H_
#define NVME_H_

#include <fayt/pci.h>
#include <fayt/bitmap.h>
#include <fayt/vector.h>
#include <fayt/portal.h>

#include <stdint.h>
#include <stddef.h>

#define NVME_PCI_BAR 0
#define NVME_QID_MAX 0xffff

constexpr size_t nvme_version_1_0_0_id = (1 << 8 | 0) << 8 | 0;
constexpr size_t nvme_version_1_1_0_id = (1 << 8 | 1) << 8 | 0;
constexpr size_t nvme_version_1_2_0_id = (1 << 8 | 2) << 8 | 0;
constexpr size_t nvme_version_1_2_1_id = (1 << 8 | 2) << 8 | 1;
constexpr size_t nvme_version_1_3_0_id = (1 << 8 | 3) << 8 | 0;
constexpr size_t nvme_version_1_4_0_id = (1 << 8 | 4) << 8 | 0;

struct [[gnu::packed]] nvme_regs {
	uint64_t cap;   
	uint32_t vs;  
	uint32_t intms;
	uint32_t intmc;
	uint32_t cc; 
	uint32_t rsvd1; 
	uint32_t csts;
	uint32_t rsvd2;
	uint32_t aqa; 
	uint64_t asq; 
	uint64_t acq;
};

constexpr size_t nvme_op_del_sq = 0x0;
constexpr size_t nvme_op_create_sq = 0x1;
constexpr size_t nvme_op_delete_cq = 0x4;
constexpr size_t nvme_op_create_cq = 0x5;
constexpr size_t nvme_op_identify = 0x6;
constexpr size_t nvme_op_abort = 0x8;
constexpr size_t nvme_op_set_features = 0x9;
constexpr size_t nvme_op_get_features = 0xa;
constexpr size_t nvme_op_ns_management = 0xd;
constexpr size_t nvme_op_format_command = 0x80;

struct [[gnu::packed]] nvme_command_create_cq {
	uint32_t rsvd1[5];
	uint64_t prp1;
	uint64_t rsvd8;
	uint16_t cqid;
	uint16_t qsize;
	uint16_t cq_flags;
	uint16_t irq_vector;
	uint32_t rsvd12[4];
};

struct [[gnu::packed]] nvme_command_create_sq {
	uint32_t rsvd1[5];
	uint64_t prp1;
	uint64_t rsvd8;
	uint16_t sqid;
	uint16_t qsize;
	uint16_t sq_flags;
	uint16_t cqid;
	uint32_t rsvd12[4];
};

struct [[gnu::packed]] nvme_command_delete_queue {
	uint32_t rsvd1[9];
	uint16_t qid;
	uint16_t rsvd10;
	uint32_t rsvd11[5];
};

struct [[gnu::packed]] nvme_command_abort {
	uint32_t rsvd1[9];
	uint16_t sqid;
	uint16_t cid;
	uint32_t rsvd11[5];
};

struct [[gnu::packed]] nvme_command_features {
	uint32_t nsid;
	uint64_t rsvd2[2];
	uint64_t prp1;
	uint64_t prp2;
	uint32_t fid;
	uint32_t dword11;
	uint32_t rsvd12[4];
};

struct [[gnu::packed]] nvme_command_identify {
	uint32_t nsid;
	uint64_t rsvd2[2];
	uint64_t prp1;
	uint64_t prp2;
	uint32_t cns;
	uint32_t rsvd11[5];
};

struct [[gnu::packed]] nvme_command_rw {
	uint32_t nsid;
	uint64_t rsvd2;
	uint64_t metadata;
	uint64_t prp1;
	uint64_t prp2;
	uint64_t slba;
	uint16_t length;
	uint16_t control;
	uint32_t dsmgmt;
	uint32_t reftag;
	uint16_t apptag;
	uint16_t appmask;
};

struct [[gnu::packed]] nvme_command {
	uint8_t opcode;
	uint8_t flags;
	uint16_t cid;

	union {
		struct nvme_command_create_cq create_cq;
		struct nvme_command_create_sq create_sq;
		struct nvme_command_delete_queue delete_queue;
		struct nvme_command_abort abort;
		struct nvme_command_features features;
		struct nvme_command_identify identify;
		struct nvme_command_rw rw;
	} private;
};

struct [[gnu::packed]] nvme_completion {
	uint32_t result;
	uint32_t rsvd;
	uint16_t sq_head;
	uint16_t sq_id;
	uint16_t cid;
	uint16_t status;
};

struct [[gnu::packed]] nvme_power_state_id {
	uint16_t max_power;
	uint8_t rsvd2;
	uint8_t flags;
	uint32_t entry_lat;
	uint32_t exit_lat;
	uint8_t read_tput;
	uint8_t read_lat;
	uint8_t write_tput;
	uint8_t write_lat;
	uint16_t idle_power;
	uint8_t idle_scale;
	uint8_t rsvd19;
	uint16_t active_power;
	uint8_t active_work_scale;
	uint8_t rsvd23[9];
};

struct [[gnu::packed]] nvme_controller_id {
	uint16_t vid;
	uint16_t ssvid;
	char sn[20];
	char mn[40];
	char fr[8];
	uint8_t rab;
	uint8_t ieee[3];
	uint8_t mic;
	uint8_t mdts;
	uint16_t cntlid;
	uint32_t ver;
	uint8_t rsvd84[172];
	uint16_t oacs;
	uint8_t acl;
	uint8_t aerl;
	uint8_t frmw;
	uint8_t lpa;
	uint8_t elpe;
	uint8_t npss;
	uint8_t avscc;
	uint8_t apsta;
	uint16_t wctemp;
	uint16_t cctemp;
	uint8_t rsvd270[242];
	uint8_t sqes;
	uint8_t cqes;
	uint8_t rsvd514[2];
	uint32_t nn;
	uint16_t oncs;
	uint16_t fuses;
	uint8_t fna;
	uint8_t vwc;
	uint16_t awun;
	uint16_t awupf;
	uint8_t nvscc;
	uint8_t rsvd531;
	uint16_t acwu;
	uint8_t rsvd534[2];
	uint32_t sgls;
	uint8_t rsvd540[1508];
	struct nvme_power_state_id psd[32];
	uint8_t vs[1024];
};

struct [[gnu::packed]] nvme_lbaf {
	uint16_t ms;
	uint8_t ds;
	uint8_t rp;
};

struct [[gnu::packed]] nvme_namespace_id {
	uint64_t nsze;
	uint64_t ncap;
	uint64_t nuse;
	uint8_t nsfeat;
	uint8_t nlbaf;
	uint8_t flbas;
	uint8_t mc;
	uint8_t dpc;
	uint8_t dps;
	uint8_t nmic;
	uint8_t rescap;
	uint8_t fpi;
	uint8_t rsvd33;
	uint16_t nawun;
	uint16_t nawupf;
	uint16_t nacwu;
	uint16_t nabsn;
	uint16_t nabo;
	uint16_t nabspf;
	uint16_t rsvd46;
	uint64_t nvmcap[2];
	uint8_t rsvd64[40];
	uint8_t nguid[16];
	uint8_t eui64[8];
	struct nvme_lbaf lbaf_list[16];
	uint8_t rsvd192[192];
	uint8_t vs[3712];
};

struct nvme_queue_entry {
	struct nvme_completion completion;
	int cid;
	int response;
	int blocking;
};

#define NVME_QUEUE_NAME_LENGTH 16

struct nvme_controller;
struct nvme_queue_pair {
	int qid;
	int entry_cnt;
	int sq_head;
	int sq_tail;
	int cq_head;
	int cq_tail;
	bool phase;
	int vector;
	int irq;
	bool admin;

	struct nvme_controller *controller;

	uint64_t submission_queue_paddr;
	volatile struct nvme_command *submission_queue;

	uint64_t completion_queue_paddr;
	volatile struct nvme_completion *completion_queue;

	int submission_doorbell_offset;
	volatile uint32_t *submission_doorbell;

	int completion_doorbell_offset;
	volatile uint32_t *completion_doorbell;

	char name[NVME_QUEUE_NAME_LENGTH];

	uint64_t queue_entry_paddr;
	struct nvme_queue_entry *queue_entry;
	int queue_entry_cnt;

	struct bitmap cid_bitmap;
};

struct nvme_namespace {
	int nsid;
	struct nvme_namespace_id identity;

	int max_prp;
	int lba_cnt;
	int lba_size;

	struct portal_resp portal_resp_prp;
	uint64_t *prp_list;

	struct nvme_queue_pair *queue_pair;
};

struct nvme_controller {
	volatile struct nvme_regs *regs;
	volatile struct nvme_controller_id *id;

	struct {
		int major;
		int minor;
		int tertiary;
	} version;

	int queue_entries;
	int page_size_max;
	int page_size_min;
	int page_size;
	int max_transfer_shift;
	int max_prp;
	int strides;

	struct bitmap qid_bitmap;
	struct nvme_queue_pair *admin_queue;

	struct nvme_controller_id *controller_id;
	VECTOR(struct nvme_namespace*) namespace;

	int nvme_queue_pair_cnt;
	struct nvme_queue_pair nvme_queue_pair[];
};

int nvme(struct pci_info*);

#endif
