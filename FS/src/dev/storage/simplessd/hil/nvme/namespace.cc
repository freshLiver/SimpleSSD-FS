/*
 * Copyright (C) 2017 CAMELab
 *
 * This file is part of SimpleSSD.
 *
 * SimpleSSD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleSSD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "hil/nvme/namespace.hh"

#include "hil/nvme/subsystem.hh"
#include "util/algorithm.hh"

#include "isc/runtime.hh"

using namespace SimpleSSD::ISC;

namespace SimpleSSD {

namespace HIL {

namespace NVMe {

Namespace::Namespace(Subsystem *p, ConfigData &c)
    : pParent(p),
      pDisk(nullptr),
      cfgdata(c),
      conf(*c.pConfigReader),
      nsid(NSID_NONE),
      attached(false),
      allocated(false),
      formatFinishedAt(0) {
  // insts for testing
  const byte insts_LOWERCASE[] = {
      0xf3, 0x0f, 0x1e, 0xfa, 0x55, 0x48, 0x89, 0xe5, 0x48, 0x89, 0x7d, 0xe8,
      0x48, 0x89, 0x75, 0xe0, 0x48, 0x8b, 0x45, 0xe8, 0x48, 0x89, 0x45, 0xf8,
      0xc7, 0x45, 0xf4, 0x00, 0x00, 0x00, 0x00, 0x8b, 0x45, 0xf4, 0x48, 0x98,
      0x48, 0x39, 0x45, 0xe0, 0x76, 0x52, 0x8b, 0x45, 0xf4, 0x48, 0x63, 0xd0,
      0x48, 0x8b, 0x45, 0xf8, 0x48, 0x01, 0xd0, 0x0f, 0xb6, 0x00, 0x3c, 0x60,
      0x7e, 0x38, 0x8b, 0x45, 0xf4, 0x48, 0x63, 0xd0, 0x48, 0x8b, 0x45, 0xf8,
      0x48, 0x01, 0xd0, 0x0f, 0xb6, 0x00, 0x3c, 0x7a, 0x7f, 0x24, 0x8b, 0x45,
      0xf4, 0x48, 0x63, 0xd0, 0x48, 0x8b, 0x45, 0xf8, 0x48, 0x01, 0xd0, 0x0f,
      0xb6, 0x00, 0x8d, 0x48, 0xe0, 0x8b, 0x45, 0xf4, 0x48, 0x63, 0xd0, 0x48,
      0x8b, 0x45, 0xf8, 0x48, 0x01, 0xd0, 0x89, 0xca, 0x88, 0x10, 0x83, 0x45,
      0xf4, 0x01, 0xeb, 0xa3, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x5d, 0xc3};
  const size_t szInst = sizeof(insts_LOWERCASE);
  const size_t szPkt = GenericSlet::PACKET_STARTUP_OFFSET + szInst;

  // init packet
  byte pkt[szPkt] = {0};
  *(GenericSlet::Packet *)pkt = GenericSlet::Packet(SletType::APP);
  GenericSlet::setPktStartup(pkt, insts_LOWERCASE, szInst);

  ISC_STS_SLET_ID id;
  assert((id = Runtime::addSlet(pkt, szPkt)) >= ISC_STS_OK);
}

Namespace::~Namespace() {
  assert(ISC_STS_OK == Runtime::delSlet(0));
  if (pDisk) {
    delete pDisk;
  }
}

void Namespace::submitCommand(SQEntryWrapper &req, RequestFunction &func) {
  CQEntryWrapper resp(req);
  bool response = false;

  if (getTick() < formatFinishedAt) {
    resp.makeStatus(false, false, TYPE_GENERIC_COMMAND_STATUS,
                    STATUS_FORMAT_IN_PROGRESS);

    response = true;
  }
  else {
    // Admin commands
    if (req.sqID == 0) {
      switch (req.entry.dword0.opcode) {
        case OPCODE_GET_LOG_PAGE:
          getLogPage(req, func);
          break;
        default:
          resp.makeStatus(true, false, TYPE_GENERIC_COMMAND_STATUS,
                          STATUS_INVALID_OPCODE);

          response = true;

          break;
      }
    }

    // NVM commands
    else {
      switch (req.entry.dword0.opcode) {
        case OPCODE_FLUSH:
          flush(req, func);
          break;
        case OPCODE_WRITE:
          write(req, func);
          break;
        case OPCODE_READ:
          read(req, func);
          break;
        case OPCODE_ISC:
          isc(req, func);
          break;
        case OPCODE_COMPARE:
          compare(req, func);
          break;
        case OPCODE_DATASET_MANAGEMEMT:
          datasetManagement(req, func);
          break;
        default:
          resp.makeStatus(true, false, TYPE_GENERIC_COMMAND_STATUS,
                          STATUS_INVALID_OPCODE);

          response = true;

          break;
      }
    }
  }

  if (response) {
    func(resp);
  }
}

void Namespace::setData(uint32_t id, Information *data) {
  nsid = id;
  memcpy(&info, data, sizeof(Information));

  if (conf.readBoolean(CONFIG_NVME, NVME_ENABLE_DISK_IMAGE)) {
    uint64_t diskSize;

    std::string filename =
        conf.readString(CONFIG_NVME, NVME_DISK_IMAGE_PATH + nsid);

    if (filename.length() == 0) {
      pDisk = new MemDisk();
    }
    else if (conf.readBoolean(CONFIG_NVME, NVME_USE_COW_DISK)) {
      pDisk = new CoWDisk();
    }
    else {
      pDisk = new Disk();
    }

    diskSize = pDisk->open(filename, info.size * info.lbaSize, info.lbaSize);

    if (diskSize == 0) {
      panic("Failed to open disk image");
    }
    else if (diskSize != info.size * info.lbaSize) {
      if (conf.readBoolean(CONFIG_NVME, NVME_STRICT_DISK_SIZE)) {
        panic("Disk size not match");
      }
    }

    if (filename.length() > 0) {
      SimpleSSD::info("Using disk image at %s for NSID %u", filename.c_str(),
                      nsid);
    }
  }

  allocated = true;
}

void Namespace::attach(bool attach) {
  attached = attach;
}

uint32_t Namespace::getNSID() {
  return nsid;
}

Namespace::Information *Namespace::getInfo() {
  return &info;
}

bool Namespace::isAttached() {
  return attached;
}

void Namespace::format(uint64_t tick) {
  formatFinishedAt = tick;

  health = HealthInfo();

  if (pDisk) {
    delete pDisk;
    pDisk = nullptr;
  }
}

void Namespace::getLogPage(SQEntryWrapper &req, RequestFunction &func) {
  CQEntryWrapper resp(req);
  uint16_t numdl = (req.entry.dword10 & 0xFFFF0000) >> 16;
  uint16_t lid = req.entry.dword10 & 0xFFFF;
  uint16_t numdu = req.entry.dword11 & 0xFFFF;
  uint32_t lopl = req.entry.dword12;
  uint32_t lopu = req.entry.dword13;
  bool submit = true;

  uint32_t req_size = (((uint32_t)numdu << 16 | numdl) + 1) * 4;
  uint64_t offset = ((uint64_t)lopu << 32) | lopl;

  debugprint(LOG_HIL_NVME,
             "ADMIN   | Get Log Page | Log %d | Size %d | NSID %d", lid,
             req_size, nsid);

  static DMAFunction dmaDone = [](uint64_t, void *context) {
    RequestContext *pContext = (RequestContext *)context;

    pContext->function(pContext->resp);

    delete pContext->dma;
    delete pContext;
  };
  DMAFunction smartInfo = [offset](uint64_t, void *context) {
    RequestContext *pContext = (RequestContext *)context;

    pContext->dma->write(offset, 512, pContext->buffer, dmaDone, context);
  };

  switch (lid) {
    case LOG_SMART_HEALTH_INFORMATION:
      if (req.entry.namespaceID == nsid) {
        submit = false;
        RequestContext *pContext = new RequestContext(func, resp);

        pContext->buffer = health.data;

        if (req.useSGL) {
          pContext->dma = new SGL(cfgdata, smartInfo, pContext, req.entry.data1,
                                  req.entry.data2);
        }
        else {
          pContext->dma =
              new PRPList(cfgdata, smartInfo, pContext, req.entry.data1,
                          req.entry.data2, (uint64_t)req_size);
        }
      }
      else {
        resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                        STATUS_NAMESPACE_NOT_ATTACHED);
      }

      break;
    default:
      resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                      STATUS_INVALID_LOG_PAGE);
      break;
  }

  if (submit) {
    func(resp);
  }
}

void Namespace::flush(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);

  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);

    func(resp);
  }

  if (!err) {
    DMAFunction begin = [this](uint64_t, void *context) {
      DMAFunction doFlush = [this](uint64_t now, void *context) {
        IOContext *pContext = (IOContext *)context;

        debugprint(
            LOG_HIL_NVME,
            "NVM     | FLUSH | CQ %u | SQ %u:%u | CID %u | NSID %-5d | %" PRIu64
            " - %" PRIu64 " (%" PRIu64 ")",
            pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
            pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
            pContext->beginAt, now, now - pContext->beginAt);

        pContext->function(pContext->resp);

        delete pContext;
      };

      pParent->flush(this, doFlush, context);
    };

    debugprint(LOG_HIL_NVME, "NVM     | FLUSH | SQ %u:%u | CID %u |  NSID %-5d",
               req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();

    execute(CPU::NVME__NAMESPACE, CPU::FLUSH, begin, pContext);
  }
}

void Namespace::write(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);
  uint64_t slba = ((uint64_t)req.entry.dword11 << 32) | req.entry.dword10;
  uint16_t nlb = (req.entry.dword12 & 0xFFFF) + 1;

  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if (nlb == 0) {
    err = true;
    warn("nvme_namespace: host tried to write 0 blocks");
  }

  debugprint(LOG_HIL_NVME,
             "NVM     | WRITE | SQ %u:%u | CID %u | NSID %-5d | %" PRIX64
             " + %d",
             req.sqID, req.sqUID, req.entry.dword0.commandID, nsid, slba, nlb);

  if (!err) {
    DMAFunction doRead = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        IOContext *pContext = (IOContext *)context;

        pContext->beginAt++;

        if (pContext->beginAt == 2) {
          debugprint(
              LOG_HIL_NVME,
              "NVM     | WRITE | CQ %u | SQ %u:%u | CID %u | NSID %-5d | "
              "%" PRIX64 " + %d | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
              pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
              pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
              pContext->slba, pContext->nlb, pContext->tick, tick,
              tick - pContext->tick);
          pContext->function(pContext->resp);

          if (pContext->buffer) {
            pDisk->write(pContext->slba, pContext->nlb, pContext->buffer);

            free(pContext->buffer);
          }

          delete pContext->dma;
          delete pContext;
        }
      };

      IOContext *pContext = (IOContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;

      if (pDisk) {
        pContext->buffer = (uint8_t *)calloc(pContext->nlb, info.lbaSize);

        pContext->dma->read(0, pContext->nlb * info.lbaSize, pContext->buffer,
                            dmaDone, context);
      }
      else {
        pContext->dma->read(0, pContext->nlb * info.lbaSize, nullptr, dmaDone,
                            context);
      }

      pParent->write(this, pContext->slba, pContext->nlb, dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    pContext->slba = slba;
    pContext->nlb = nlb;

    CPUContext *pCPU =
        new CPUContext(doRead, pContext, CPU::NVME__NAMESPACE, CPU::WRITE);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, (uint64_t)nlb * info.lbaSize);
    }
  }
  else {
    func(resp);
  }
}

void Namespace::read(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);
  uint64_t slba = ((uint64_t)req.entry.dword11 << 32) | req.entry.dword10;
  uint16_t nlb = (req.entry.dword12 & 0xFFFF) + 1;
  // bool fua = req.entry.dword12 & 0x40000000;

  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if (nlb == 0) {
    err = true;
    warn("nvme_namespace: host tried to read 0 blocks");
  }

  debugprint(LOG_HIL_NVME,
             "NVM     | READ  | SQ %u:%u | CID %u | NSID %-5d | %" PRIX64
             " + %d",
             req.sqID, req.sqUID, req.entry.dword0.commandID, nsid, slba, nlb);

  if (!err) {
    DMAFunction doRead = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        IOContext *pContext = (IOContext *)context;

        pContext->beginAt++;

        if (pContext->beginAt == 2) {
          debugprint(
              LOG_HIL_NVME,
              "NVM     | READ  | CQ %u | SQ %u:%u | CID %u | NSID %-5d | "
              "%" PRIX64 " + %d | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
              pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
              pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
              pContext->slba, pContext->nlb, pContext->tick, tick,
              tick - pContext->tick);

          pContext->function(pContext->resp);

          if (pContext->buffer) {
            free(pContext->buffer);
          }

          delete pContext->dma;
          delete pContext;
        }
      };

      IOContext *pContext = (IOContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;

      pParent->read(this, pContext->slba, pContext->nlb, dmaDone, pContext);

      pContext->buffer = (uint8_t *)calloc(pContext->nlb, info.lbaSize);

      if (pDisk) {
        pDisk->read(pContext->slba, pContext->nlb, pContext->buffer);
      }
      pContext->dma->write(0, pContext->nlb * info.lbaSize, pContext->buffer,
                           dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    pContext->slba = slba;
    pContext->nlb = nlb;

    CPUContext *pCPU =
        new CPUContext(doRead, pContext, CPU::NVME__NAMESPACE, CPU::READ);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, pContext->nlb * info.lbaSize);
    }
  }
  else {
    func(resp);
  }
}

void Namespace::isc(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);
  uint64_t slba = req.entry.dword10;
  uint32_t sletid = (uint64_t)req.entry.dword11;
  uint16_t nlb = (req.entry.dword12 & 0xFFFF) + 1;
  // bool fua = req.entry.dword12 & 0x40000000;

  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if (nlb == 0) {
    err = true;
    warn("nvme_namespace: host tried to read 0 blocks");
  }

  debugprint(LOG_HIL_NVME,
             "NVM     | ISC  | SQ %u:%u | CID %u | NSID %-5d | %" PRIX64
             " + %d",
             req.sqID, req.sqUID, req.entry.dword0.commandID, nsid, slba, nlb);

  if (!err) {
    DMAFunction doRead = [this, sletid](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        IOContext *pContext = (IOContext *)context;

        pContext->beginAt++;

        if (pContext->beginAt == 2) {
          debugprint(
              LOG_HIL_NVME,
              "NVM     | ISC  | CQ %u | SQ %u:%u | CID %u | NSID %-5d | "
              "%" PRIX64 " + %d | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
              pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
              pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
              pContext->slba, pContext->nlb, pContext->tick, tick,
              tick - pContext->tick);

          pContext->function(pContext->resp);

          if (pContext->buffer) {
            free(pContext->buffer);
          }

          delete pContext->dma;
          delete pContext;
        }
      };

      ISC_STS sts = ISC_STS_FAIL;
      IOContext *pContext = (IOContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;

      pParent->read(this, pContext->slba, pContext->nlb, dmaDone, pContext);

      pContext->buffer = (uint8_t *)calloc(pContext->nlb, info.lbaSize);

      if (pDisk) {
        pDisk->read(pContext->slba, pContext->nlb, pContext->buffer);
      }
      // FIXME: should slet be executed after FTL finished?
      debugprint(LOG_HIL_NVME, "NVM     | ISC  | Try start slet %u", sletid);
      sts = Runtime::startSlet(sletid, pContext->buffer,
                               pContext->nlb * info.lbaSize);
      pContext->resp.entry.dword3.status = sts;
      pContext->dma->write(0, pContext->nlb * info.lbaSize, pContext->buffer,
                           dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    pContext->slba = slba;
    pContext->nlb = nlb;

    CPUContext *pCPU =
        new CPUContext(doRead, pContext, CPU::NVME__NAMESPACE, CPU::READ);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, pContext->nlb * info.lbaSize);
    }
  }
  else {
    func(resp);
  }
}

void Namespace::compare(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);
  uint64_t slba = ((uint64_t)req.entry.dword11 << 32) | req.entry.dword10;
  uint16_t nlb = (req.entry.dword12 & 0xFFFF) + 1;
  // bool fua = req.entry.dword12 & 0x40000000;

  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if (nlb == 0) {
    err = true;
    warn("nvme_namespace: host tried to read 0 blocks");
  }

  debugprint(LOG_HIL_NVME,
             "NVM     | COMP  | SQ %u:%u | CID %u | NSID %-5d | %" PRIX64
             " + %d",
             req.sqID, req.sqUID, req.entry.dword0.commandID, nsid, slba, nlb);

  if (!err) {
    DMAFunction doRead = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        CompareContext *pContext = (CompareContext *)context;

        pContext->beginAt++;

        if (pContext->beginAt == 2) {
          // Compare buffer!
          // Always success if no disk
          if (pDisk && memcmp(pContext->buffer, pContext->hostContent,
                              pContext->nlb * info.lbaSize) != 0) {
            pContext->resp.makeStatus(false, false,
                                      TYPE_MEDIA_AND_DATA_INTEGRITY_ERROR,
                                      STATUS_COMPARE_FAILURE);
          }

          debugprint(
              LOG_HIL_NVME,
              "NVM     | COMP  | CQ %u | SQ %u:%u | CID %u | NSID %-5d | "
              "%" PRIX64 " + %d | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
              pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
              pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
              pContext->slba, pContext->nlb, pContext->tick, tick,
              tick - pContext->tick);

          pContext->function(pContext->resp);

          if (pContext->buffer) {
            free(pContext->buffer);
          }
          if (pContext->hostContent) {
            free(pContext->hostContent);
          }

          delete pContext->dma;
          delete pContext;
        }
      };

      CompareContext *pContext = (CompareContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;

      pParent->read(this, pContext->slba, pContext->nlb, dmaDone, pContext);

      pContext->buffer = (uint8_t *)calloc(pContext->nlb, info.lbaSize);
      pContext->hostContent = (uint8_t *)calloc(pContext->nlb, info.lbaSize);

      if (pDisk) {
        pDisk->read(pContext->slba, pContext->nlb, pContext->buffer);
      }

      pContext->dma->read(0, pContext->nlb * info.lbaSize,
                          pContext->hostContent, dmaDone, context);
    };

    CompareContext *pContext = new CompareContext(func, resp);

    pContext->beginAt = getTick();
    pContext->slba = slba;
    pContext->nlb = nlb;

    CPUContext *pCPU =
        new CPUContext(doRead, pContext, CPU::NVME__NAMESPACE, CPU::READ);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, pContext->nlb * info.lbaSize);
    }
  }
  else {
    func(resp);
  }
}

void Namespace::datasetManagement(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);
  int nr = (req.entry.dword10 & 0xFF) + 1;
  bool ad = req.entry.dword11 & 0x04;

  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if (!ad) {
    err = true;
    // Just ignore
  }

  debugprint(
      LOG_HIL_NVME,
      "NVM     | TRIM  | SQ %u:%u | CID %u |  NSID %-5d| %d ranges | Attr %1X",
      req.sqID, req.sqUID, req.entry.dword0.commandID, nsid, nr,
      req.entry.dword11 & 0x0F);

  if (!err) {
    static DMAFunction eachTrimDone = [](uint64_t tick, void *context) {
      DMAContext *pContext = (DMAContext *)context;

      pContext->counter--;

      if (pContext->counter == 0) {
        pContext->function(tick, pContext->context);
      }

      delete pContext;
    };
    DMAFunction doTrim = [this](uint64_t, void *context) {
      DMAFunction dmaDone = [this](uint64_t, void *context) {
        DMAFunction trimDone = [this](uint64_t tick, void *context) {
          IOContext *pContext = (IOContext *)context;

          debugprint(LOG_HIL_NVME,
                     "NVM     | TRIM  | CQ %u | SQ %u:%u | CID %u | NSID %-5d| "
                     "%" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
                     pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
                     pContext->resp.sqUID,
                     pContext->resp.entry.dword3.commandID, nsid,
                     pContext->beginAt, tick, tick - pContext->beginAt);

          pContext->function(pContext->resp);

          delete pContext;
        };

        DatasetManagementRange range;
        IOContext *pContext = (IOContext *)context;
        DMAContext *pDMA = new DMAContext(trimDone);

        pDMA->context = context;

        for (uint64_t i = 0; i < pContext->slba; i++) {
          memcpy(range.data,
                 pContext->buffer + i * sizeof(DatasetManagementRange),
                 sizeof(DatasetManagementRange));

          pDMA->counter++;
          pParent->trim(this, range.slba, range.nlb, eachTrimDone, pDMA);
        }

        if (pDMA->counter == 0) {
          pDMA->counter = 1;

          eachTrimDone(getTick(), pDMA);
        }

        free(pContext->buffer);
        delete pContext->dma;
      };

      IOContext *pContext = (IOContext *)context;

      pContext->buffer =
          (uint8_t *)calloc(pContext->slba, sizeof(DatasetManagementRange));

      pContext->dma->read(0, pContext->slba * sizeof(DatasetManagementRange),
                          pContext->buffer, dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    pContext->slba = nr;

    CPUContext *pCPU = new CPUContext(doTrim, pContext, CPU::NVME__NAMESPACE,
                                      CPU::DATASET_MANAGEMENT);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma = new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                                  req.entry.data2, (uint64_t)nr * 0x10);
    }
  }
  else {
    func(resp);
  }
}

}  // namespace NVMe

}  // namespace HIL

}  // namespace SimpleSSD
