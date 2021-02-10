#include <linux/iio/iio.h>
#include <linux/iio/types.h>
#include <linux/iio/sysfs.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/interrupt.h>

#define TH02_STATUS 0x0
#define TH02_DATA   0x1
#define TH02_CONFIG 0x3

#define TH02_CONFIG_START 0x0
#define TH02_CONFIG_TEMP 0x4

#define TH02_MAX_RETRY 0x10

struct th02_device {
  struct i2c_client *client;
	struct mutex lock;
};

static const struct iio_chan_spec th02_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
	},
  { .type = IIO_HUMIDITYRELATIVE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
  }
};

static int read_status(struct i2c_client *client, u8 *status)
{
	int ret;
	u8 reg = TH02_STATUS;
	ret = i2c_master_send(client, &reg, 1);
	if (ret < 0)
	{
		dev_err(&client->dev, "%s: failed to send data\n", __func__);
		return ret;
	}
	ret = i2c_master_recv(client, status, 1);
	if (ret < 0)
	{
		dev_err(&client->dev, "%s: failed to receive data\n", __func__);
		return ret;
	}

	return 0;
}

// the size of buffer must be 2.
static int read_data(struct i2c_client *client, u8 *buf)
{
	int ret;
	u8 addr = TH02_DATA;
	ret = i2c_master_send(client, &addr, 1);
	if (ret < 0)
	{
		dev_err(&client->dev, "%s: failed to send data\n", __func__);
		return ret;
	}
	ret = i2c_master_recv(client, buf, 2);
	if (ret < 0)
	{
		dev_err(&client->dev, "%s: failed to receive data\n", __func__);
		return ret;
	}

	if (ret != 2)
		return -EIO;

	return 0;
}

static int write_config(struct i2c_client *client, u8 config)
{
	int ret;
	u8 buf[2];

  buf[0] = TH02_CONFIG;
	buf[1] = config;
	
	ret = i2c_master_send(client, buf, 2);
	if (ret < 0)
	{
		dev_err(&client->dev, "%s: failed to send data\n", __func__);
		return ret;
	}

	return 0;
}

static int read_sensor(struct th02_device *device, enum iio_chan_type type, s32 *result)
{
	struct i2c_client *client = device->client;
	int ret, polling = 0;
	u8 buf[2] = {0};
	u8 config = 0, status = 1;

	switch (type)
	{
		case IIO_TEMP:
			config = BIT(TH02_CONFIG_TEMP) | BIT(TH02_CONFIG_START);
			break;
		case IIO_HUMIDITYRELATIVE:
			config = BIT(TH02_CONFIG_START);
			break;
		default:
			dev_err(&client->dev, "%s: invalid sensor type %d\n", __func__, type);
			return -EINVAL;
	}

	mutex_lock(&device->lock);

	ret = write_config(client, config);
	if (ret < 0)
	{
		dev_err(&client->dev, "%s: failed to send data\n", __func__);
		goto err;
	}
	
	while(status != 0 && polling++ != TH02_MAX_RETRY)
	{
		msleep(1);
		ret = read_status(client, &status);
		dev_dbg(&client->dev, "status: %02x\n", status);
		if (ret < 0)
			goto err;
	}

	ret = read_data(client, buf);
	if (ret < 0)
		goto err;

	dev_dbg(&client->dev, "data: %02x %02x", buf[1], buf[0]);

	switch (type)
	{
		case IIO_TEMP:
			*result = ((buf[1] << 6 | buf[0] >> 2) / 32) - 50;
			break;
		case IIO_HUMIDITYRELATIVE:
			*result = ((buf[1] << 4 | buf[0] >> 4) / 16) - 24;
			break;
		default:
			dev_err(&client->dev, "%s: invalid sensor type %d\n", __func__, type);
			ret = -EINVAL;
			goto err;
	}
	ret = 0;

err:
	mutex_unlock(&device->lock);
	return ret;
}

static int th02_read_raw(
    struct iio_dev *th02_iio,
    struct iio_chan_spec const *channel, int *val,
    int *val2, long mask)
{
	int ret;
	s32 result;
	struct th02_device *th02_dev = iio_priv(th02_iio);

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		switch (channel->type) {
		case IIO_TEMP:
		case IIO_HUMIDITYRELATIVE:
			ret = read_sensor(th02_dev, channel->type, &result);
			if (ret)
				return ret;

			*val = result;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static const struct iio_info th02_info = {
	.read_raw = th02_read_raw,
};

static int th02_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct th02_device *th02_dev;
	struct iio_dev *th02_iio;

	th02_iio = devm_iio_device_alloc(&client->dev, sizeof(struct th02_device));
	if (!th02_iio)
		return -ENOMEM;

	th02_dev = iio_priv(th02_iio);
	th02_dev->client = client;
  mutex_init(&th02_dev->lock);

  th02_iio->name = id->name;
	th02_iio->info = &th02_info;
	th02_iio->modes = INDIO_DIRECT_MODE;
  th02_iio->channels = th02_channels;
  th02_iio->num_channels = ARRAY_SIZE(th02_channels);

	i2c_set_clientdata(client, th02_iio);

	return devm_iio_device_register(&client->dev, th02_iio);
}

static const struct i2c_device_id th02_id[] = {
	{"th02", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, th02_id);

static const struct of_device_id th02_of_match[] = {
	{ .compatible = "cherie,th02", },
	{ },
};
MODULE_DEVICE_TABLE(of, th02_of_match);

static struct i2c_driver th02_driver = {
	.probe = th02_probe,
	.id_table = th02_id,
	.driver = {
		   .name = "th02",
		   .of_match_table = th02_of_match,
		   },
};

module_i2c_driver(th02_driver);

MODULE_DESCRIPTION("TH02 Temperature Humidity Sensor driver");
MODULE_AUTHOR("Cherie Hsieh <cjamhe01385@gmail.coms>");
MODULE_LICENSE("GPL v2");