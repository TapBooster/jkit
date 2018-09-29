#ifndef SOCI_PGSQL_H
#define SOCI_PGSQL_H

#include "json.hpp"
#include <ctime>
#include <atomic>
#include <memory>
#include <string>
#include <functional>
#include <soci/soci.h>
#include <soci/postgresql/soci-postgresql.h>
#include <soci/callbacks.h>

#include <boost/uuid/uuid.hpp>
#include <boost/date_time.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>


#include "logger.h"
#include <boost/log/attributes.hpp>

using namespace nlohmann;
using namespace std;
using namespace boost::posix_time;
using namespace boost::uuids;

typedef std::unique_ptr<soci::session, std::function<void(soci::session *)>> DbSessionPtr;

namespace soci
{
template <>
struct type_conversion<bool>
{
    typedef int base_type;

    static void from_base(int i, indicator ind, bool &mi)
    {
        if (ind == i_null)
        {
            mi = false;
            return;
        }

        mi = (i != 0 ? true : false);
    }

    static void to_base(const bool &mi, int &i, indicator &ind)
    {
        i = (mi ? 1 : 0);
        ind = i_ok;
    }
};

template <>
struct type_conversion<ptime>
{
    typedef std::tm base_type;

    static void from_base(const std::tm &i, indicator ind, ptime &mi)
    {
        if (ind == i_null)
        {
            return;
        }

        mi = boost::posix_time::ptime_from_tm(i);
    }

    static void to_base(const ptime &mi, std::tm &i, indicator &ind)
    {
        if(mi.is_not_a_date_time())
        {
            ind = i_null;
        }
        else
        {
            i = boost::posix_time::to_tm(mi);
            ind = i_ok;
        }
    }
};

template <>
struct type_conversion<json>
{
    typedef std::string base_type;

    static void from_base(const std::string &i, indicator ind, json &mi)
    {
        if (ind == i_null)
        {
            return;
        }

        mi = json::parse(i);
    }

    static void to_base(const json &mi, std::string &i, indicator &ind)
    {
        if(mi.is_null())
        {
            ind = i_null;
        }
        else
        {
            i = mi.dump();
            ind = i_ok;
        }
    }
};

template <>
struct type_conversion<uuid>
{
    typedef std::string base_type;

    static void from_base(const std::string &i, indicator ind, uuid &mi)
    {
        if (ind == i_null)
        {
            mi = boost::uuids::nil_uuid();
            return;
        }

        mi = boost::lexical_cast<uuid>(i);
    }

    static void to_base(const uuid &mi, std::string &i, indicator &ind)
    {
        i = boost::lexical_cast<string>(mi);
        ind = i_ok;
    }
};
}

class PGSessionFailOverCallback : public soci::failover_callback
{
public:
    PGSessionFailOverCallback(const string& failover_conn_param) : m_failover_conn_param(failover_conn_param)
    {

    }

    //简单失败重连
    virtual void failed(bool & retry, std::string & failover_conn_param)
    {
        retry = true;
        failover_conn_param = m_failover_conn_param;
        LogDebugExt << "db faileover:" << failover_conn_param;
    }

    //faileover失败被中止
    virtual void aborted()
    {
        LogErrorExt << "db faileover aborted:" << m_failover_conn_param;
    }

private:
    string m_failover_conn_param;
};

//使用postgresql数据库之前需要调用register_factory_postgresql一次
extern "C" void register_factory_postgresql();

//数据库
class DBHelper
{
public:
    DBHelper() = default;
    void set_config(const json& config_param)
    {
        string process_name = boost::log::aux::get_process_name();
        string conn_str;
        conn_str += "host=";
        conn_str += config_param.at("db_ip").get<string>();
        conn_str += " ";
        conn_str += "port=";
        conn_str += boost::lexical_cast<string>(config_param.at("db_port").get<int>());
        conn_str += " ";
        conn_str += "dbname=";
        conn_str += config_param.at("db_name").get<string>();
        conn_str += " ";
        conn_str += "user=";
        conn_str += config_param.at("db_user").get<string>();
        conn_str += " ";
        conn_str += "password=";
        conn_str += config_param.at("db_password").get<string>();
        conn_str += " ";
        conn_str += "application_name=";
        conn_str += process_name;

        m_conn_str = std::move(conn_str);

        m_connect_num = config_param.at("db_connect_num");
    }
    void set_failover(soci::failover_callback* cb)
    {
        m_failover = cb;
    }

    string conn_str()
    {
        return m_conn_str;
    }

    DbSessionPtr session()
    {
        size_t pos = m_pool->lease();
        soci::session &pooledSession = m_pool->at(pos);
        DbSessionPtr s(&pooledSession, [pos, this](soci::session* p) {
            m_pool->give_back(pos);
        });

        return std::move(s);
    }

    void start()
    {
        m_pool.reset(new soci::connection_pool(m_connect_num));
        for (size_t i = 0; i != m_connect_num; ++i)
        {
            soci::session &sql = m_pool->at(i);
            sql.open("postgresql", m_conn_str);
            if(m_failover)
            {
                sql.set_failover_callback(*m_failover);
            }
        }
    }


    ~DBHelper() = default;

private:
    soci::failover_callback* m_failover = nullptr;
    string m_conn_str;
    int m_connect_num = 1;
    std::unique_ptr<soci::connection_pool> m_pool;
};


#endif
